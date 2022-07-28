//
// Created by vigi99 on 25/09/20.
//

#pragma once

#include <sys/stat.h>
#include <type_traits>
#include <utility>
#include <iostream>
#include "tsl/robin-map/robin_map.h"
#include "Defines.h"

#define DIFFLIB_ENABLE_EXTERN_MACROS
#include <difflib.h>

DIFFLIB_INSTANTIATE_FOR_TYPE(xstring_view);

template <typename>
struct is_std_vector : std::false_type {};

template <typename T, typename A>
struct is_std_vector<std::vector<T, A>> : std::true_type {};

template <typename>
struct is_std_string : std::false_type {};

template <typename T>
struct is_std_string<std::basic_string<T>> : std::true_type {};

template <typename>
struct is_std_pair : std::false_type {};

template <typename T, typename A>
struct is_std_pair<std::pair<T, A>> : std::true_type {};

class DifflibOptions {
public:
    enum Value : uint8_t {
        INSERT,
        DELETE,
        EQUAL,
        REPLACE};

    static const Value getType(const std::string &type) {
        if (type[0] == 'i') {
            return Value::INSERT;
        }

        if (type[0] == 'd') {
            return Value::DELETE;
        }

        if (type[0] == 'e') {
            return Value::EQUAL;
        }

        return Value::REPLACE;
    }
};

class Helpers {
public:
    static int NullDistanceResults(const xstring_view &string1, const xstring_view &string2, double maxDistance) {
        if (string1.empty())
            return (string2.empty()) ? 0 : (string2.size() <= maxDistance) ? string2.size() : -1;
        return (string1.size() <= maxDistance) ? string1.size() : -1;
    }

    static int NullSimilarityResults(const xstring_view &string1, const xstring_view &string2, double minSimilarity) {
        return (string1.empty() && string2.empty()) ? 1 : (0 <= minSimilarity) ? 0 : -1;
    }

    static void PrefixSuffixPrep(const xstring_view& string1, const xstring_view& string2, int &len1, int &len2, int &start) {
        len2 = string2.size();
        len1 = string1.size(); // this is also the minimum length of the two strings
        // suffix common to both strings can be ignored
        while (len1 != 0 && string1[len1 - 1] == string2[len2 - 1]) {
            len1 = len1 - 1;
            len2 = len2 - 1;
        }
        // prefix common to both strings can be ignored
        start = 0;
        while (start != len1 && string1[start] == string2[start]) start++;
        if (start != 0) {
            len2 -= start; // length of the part excluding common prefix and suffix
            len1 -= start;
        }
    }

    static bool StringIsUnion(const xstring_view& string, const xstring_view& left, const xstring_view& right) {
        return (
            (string.size() == left.size() + right.size()) &&
            (string.substr(0, left.size()) == left) &&
            (string.substr(left.size()) == right)
        );
    }

    static double ToSimilarity(int distance, int length) {
        return (distance < 0) ? -1 : 1 - (distance / (double) length);
    }

    static int ToDistance(double similarity, int length) {
        return (int) ((length * (1 - similarity)) + .0000000001);
    }

    static void string_upper(const xstring_view& a, xstring& out) {
        size_t const old_size = out.size();
        out.resize(old_size + a.size());
        std::transform(a.begin(), a.end(), out.begin() + old_size, to_xupper);
    }

    static void string_lower(const xstring_view& a, xstring& out) {
        size_t const old_size = out.size();
        out.resize(old_size + a.size());
        std::transform(a.begin(), a.end(), out.begin() + old_size, to_xlower);
    }

    static xstring string_lower(const xstring_view& a) {
        xstring out;
        string_lower(a, out);
        return out;
    }

    static xstring string_upper(const xstring_view& a) {
        xstring out;
        string_upper(a, out);
        return out;
    }

    static bool file_exists (const std::string& name) {
        struct stat buffer;
        return (stat (name.c_str(), &buffer) == 0);
    }

    static void transfer_casing_for_matching_text(
        const xstring_view& text_w_casing, const xstring_view& text_wo_casing,
        xstring& response_string
    ) {
        if (text_w_casing.size() != text_wo_casing.size()) {
            throw std::invalid_argument("The 'text_w_casing' and 'text_wo_casing' "
                                        "don't have the same length, "
                                        "so you can't use them with this method, "
                                        "you should be using the more general "
                                        "transfer_casing_similar_text() method.");
        }

        for (int i = 0; i < text_w_casing.size(); ++i) {
            if (is_xupper(text_w_casing[i])) {
                response_string.push_back(to_xupper(text_wo_casing[i]));
            } else {
                response_string.push_back(to_xlower(text_wo_casing[i]));
            }
        }
    }

    static xstring transfer_casing_for_similar_text(
        const xstring_view& text_w_casing, const xstring_view& text_wo_casing
    ) {
        if (text_wo_casing.empty()) {
            return "";
        }
        if (text_w_casing.empty()) {
            throw std::invalid_argument("We need 'text_w_casing' to know what casing to transfer!");
        }

        xstring const lower_text_w_casing = string_lower(text_w_casing);
        auto diff = difflib::MakeSequenceMatcher(xstring_view(lower_text_w_casing), text_wo_casing);

        xstring response_string;
        response_string.reserve(text_wo_casing.size());

        for (auto const& [ tag, i1, i2, j1, j2 ] : diff.get_opcodes()) {
            xstring_view const op_w_casing = text_w_casing.substr(i1, i2 - i1);
            xstring_view const op_wo_casing = text_wo_casing.substr(j1, j2 - j1);

            switch (DifflibOptions::getType(tag)) {
                case DifflibOptions::Value::INSERT:
                    if (i1 == 0 or (text_w_casing[i1 - 1] == ' ')) {
                        if (text_w_casing[i1] && is_xupper(text_w_casing[i1])) {
                            string_upper(op_wo_casing, response_string);
                        } else {
                            string_lower(op_wo_casing, response_string);
                        }
                    } else {
                        if (is_xupper(text_w_casing[i1 - 1])) {
                            string_upper(op_wo_casing, response_string);
                        } else {
                            string_lower(op_wo_casing, response_string);
                        }
                    }
                    break;

                case DifflibOptions::Value::DELETE:
                    break;

                case DifflibOptions::Value::REPLACE:
                    if (op_w_casing.size() == op_wo_casing.size()) {
                        transfer_casing_for_matching_text(op_w_casing, op_wo_casing, response_string);

                    } else {
                        bool last_is_upper = false;
                        const int min_length = std::min(op_w_casing.size(), op_wo_casing.size());

                        for (int i = 0; i < min_length; ++i) {
                            last_is_upper = is_xupper(op_w_casing[i]);

                            response_string.push_back(
                                last_is_upper ? to_xupper(op_wo_casing[i]) : to_xlower(op_wo_casing[i])
                            );
                        }

                        xstring_view const remain_wo_casing = op_wo_casing.substr(min_length);

                        if (last_is_upper) {
                            string_upper(remain_wo_casing, response_string);

                        } else {
                            string_lower(remain_wo_casing, response_string);
                        }
                    }
                    break;

                case DifflibOptions::Value::EQUAL :
                    response_string.append(op_w_casing);
                    break;
            }
        }

        return response_string;
    }
};

class Node {
public:
    xstring suggestion;
    int next;

    Node (const xstring_view &suggestion = {}, const int next = -1)
    : suggestion(suggestion), next(next) {}
};

class Entry {
public:
    int count;
    int first;

    Entry (int const count = 0, const int first = -1)
    : count(count), first(first) {}
};

class SuggestionStage {
private:
    tsl::robin_map<int, Entry> Deletes;
    std::deque<Node> Nodes;

public:
    explicit SuggestionStage(int initialCapacity) {
        Deletes.reserve(initialCapacity);
    }

    int DeleteCount() const { return Deletes.size(); }

    int NodeCount() const { return Nodes.size(); }

    void Clear() {
        Deletes.clear();
        Nodes.clear();
    }

    void Add(int deleteHash, const xstring_view &suggestion) {
        Entry& entry = Deletes.try_emplace(deleteHash, 0).first.value();
        int const next = entry.first;  // 1st semantic errors, this should not be Nodes.Count

        entry.count++;
        entry.first = Nodes.size();

        Nodes.emplace_back(suggestion, next);
    }

    void CommitTo(tsl::robin_map<int, std::vector<xstring>>& permanentDeletes) const {
        for (auto &Delete : Deletes) {
            auto& suggestions = permanentDeletes.try_emplace(Delete.first, 0).first.value();
            suggestions.reserve(suggestions.size() + Delete.second.count);

            for (int next = Delete.second.first; next >= 0;) {
                auto const& node = Nodes[next];
                suggestions.push_back(node.suggestion);
                next = node.next;
            }
        }
    }
};


class SuggestItem {
public:
    xstring term;
    int distance = 0;
    int64_t count = 0;

    SuggestItem() = default;

    SuggestItem(const xstring_view &term, int distance, int64_t count)
    : term(term), distance(distance), count(count) {}

    xstring Tostring() const {
        return XL("{") + term + XL(", ") + to_xstring(distance) + XL(", ") + to_xstring(count) + XL("}");
    }

    bool operator< (const SuggestItem& s2) const {
        if (this->distance != s2.distance) {
            return this->distance < s2.distance;
        }

        if (this->count != s2.count) {
            return this->count > s2.count;
        }

        return this->term < s2.term;
    }

    bool operator== (const SuggestItem& s2) const {
        return this->distance == s2.distance && this->count == s2.count && this->term == s2.term;
    }
};
