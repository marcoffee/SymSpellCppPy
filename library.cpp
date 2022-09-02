#include "library.h"

#include <codecvt>
#include <utility>
#include <fstream>

namespace symspellcpppy {
    const xregex SymSpell::wordsRegex(XL("['’\\w\\-\\[_\\]]+"), std::regex_constants::optimize);

    int SymSpell::MaxDictionaryEditDistance() const {
        return maxDictionaryEditDistance;
    }

    int SymSpell::PrefixLength() const {
        return prefixLength;
    }

    int SymSpell::MaxLength() const {
        return maxDictionaryWordLength;
    }

    long SymSpell::CountThreshold() const {
        return countThreshold;
    }

    DistanceAlgorithm SymSpell::GetDistanceAlgorithm() const {
        return distanceAlgorithm;
    }

    int SymSpell::WordCount() const {
        return words.size();
    }

    int SymSpell::EntryCount() const {
        return deletes.size();
    }

    SymSpell::SymSpell(int _maxDictionaryEditDistance, int _prefixLength, int _countThreshold,
                       unsigned char _compactLevel, DistanceAlgorithm _distanceAlgorithm,
                       double _deletesMaxLoadFactor) :
            maxDictionaryEditDistance(_maxDictionaryEditDistance),
            prefixLength(_prefixLength),
            countThreshold(_countThreshold),
            distanceAlgorithm(_distanceAlgorithm) {
        if (_maxDictionaryEditDistance < 0)
            throw std::invalid_argument("max_dictionary_edit_distance cannot be negative");
        if (_prefixLength < 1 || _prefixLength <= _maxDictionaryEditDistance)
            throw std::invalid_argument(
                    "prefix_length cannot be less than 1 or smaller than max_dictionary_edit_distance");
        if (_countThreshold < 0) throw std::invalid_argument("count_threshold cannot be negative");
        if (_compactLevel > 16) throw std::invalid_argument("compact_level cannot be greater than 16");

        if (_compactLevel > 16) _compactLevel = 16;
        compactMask = (UINT_MAX >> (3 + _compactLevel)) << 2;
        maxDictionaryWordLength = 0;

        deletes.max_load_factor(_deletesMaxLoadFactor);
    }

    words_it_t SymSpell::CreateDictionaryEntryCheck(const xstring_view &key, int64_t count) {
        if (count <= 0) {
            if (countThreshold > 0)
                return words.end(); // no point doing anything if count is zero, as it can't change anything
            count = 0;
        }

        auto belowThresholdWordsFinded = belowThresholdWords.find(key);

        if (countThreshold > 1 && belowThresholdWordsFinded != belowThresholdWords.end()) {
            int64_t& countPrevious = belowThresholdWordsFinded->second;
            count = (MAXINT - countPrevious > count) ? countPrevious + count : MAXINT;

            if (count >= countThreshold) {
                belowThresholdWords.erase(belowThresholdWordsFinded);

            } else {
                countPrevious = count;
                return words.end();
            }
        } else {
            auto wordsFinded = words.find(key);

            if (wordsFinded != words.end()) {
                int64_t& countPrevious = wordsFinded->second;
                countPrevious = (MAXINT - countPrevious > count) ? countPrevious + count : MAXINT;
                return words.end();

            } else if (count < CountThreshold()) {
                belowThresholdWords.emplace(key, count);
                return words.end();
            }
        }

        auto it = words.emplace_hint(words.end(), key, count);

        if (key.size() > maxDictionaryWordLength)
            maxDictionaryWordLength = key.size();

        return it;
    }

    bool SymSpell::CreateDictionaryEntry(const xstring_view &key, int64_t count) {
        auto ins_it = CreateDictionaryEntryCheck(key, count);

        if (ins_it == words.end()) {
            return false;
        }

        //create deletes
        auto const edits = EditsPrefix(key);

        //store deletes
        for (auto it = edits.cbegin(); it != edits.cend(); ++it) {
            deletes.try_emplace(GetstringHash(it.key_sv()), 0).first.value().emplace_back(ins_it);
        }

        return true;
    }

    bool SymSpell::CreateDictionaryEntry(const xstring_view &key, int64_t count, SuggestionStage &staging) {
        auto ins_it = CreateDictionaryEntryCheck(key, count);

        if (ins_it == words.end()) {
            return false;
        }

        //create deletes
        auto const edits = EditsPrefix(key);

        //stage deletes
        for (auto it = edits.cbegin(); it != edits.cend(); ++it) {
            staging.Add(GetstringHash(it.key_sv()), ins_it);
        }

        return true;
    }

    bool SymSpell::DeleteDictionaryEntry(const xstring_view &key) {
        auto wordsFinded = words.find(key);

        if (wordsFinded == words.end()) {
            return false;
        }

        words.erase(wordsFinded);

        if (key.size() == maxDictionaryWordLength) {
            maxDictionaryWordLength = words.empty() ? 0 : std::max_element(
                words.begin(), words.end(), [] (words_it_t const& it1, words_it_t const& it2) {
                    return it1->first.size() < it2->first.size();
                }
            )->first.size();
        }

        auto const edits = EditsPrefix(key);

        for (auto it = edits.cbegin(); it != edits.cend(); ++it) {
            auto deletesFinded = deletes.find(GetstringHash(it.key_sv()));

            if (deletesFinded == deletes.end()) {
                continue;
            }

            auto& delete_vec = deletesFinded.value();

            delete_vec.erase(
                std::remove(delete_vec.begin(), delete_vec.end(), wordsFinded),
                delete_vec.end()
            );

            if (delete_vec.empty()) {
                deletes.erase(deletesFinded);
            }
        }

        return true;
    }

    bool
    SymSpell::LoadBigramDictionary(const std::string &corpus, int termIndex, int countIndex, xchar separatorChars) {
        xifstream corpusStream;
        corpusStream.open(corpus);
#ifdef UNICODE_SUPPORT
        std::locale utf8(std::locale(), new std::codecvt_utf8<wchar_t>);
        corpusStream.imbue(utf8);
#endif
        if (!corpusStream.is_open())
            return false;

        return LoadBigramDictionary(corpusStream, termIndex, countIndex, separatorChars);
    }

    bool SymSpell::LoadBigramDictionary(xifstream &corpusStream, int termIndex, int countIndex, xchar separatorChars) {
        int const termAdd = (separatorChars == DEFAULT_SEPARATOR_CHAR) ? 1 : 0;
        int const secondTermIndex = termIndex + termAdd;
        int const minTerms = std::max(secondTermIndex, countIndex);
        int const termCount = termAdd + 1;
        xstring line;

        while (getline(corpusStream, line)) {
            int64_t count = 1;
            std::vector<xstring_view> tokens;
            size_t linePos = 0;
            xstring_view term;

            tokens.reserve(termCount);

            for (int i = 0; i <= minTerms && Helpers::split_line(line, separatorChars, linePos, term); ++i) {
                if (i == termIndex || i == secondTermIndex) {
                    tokens.emplace_back(term);
                }

                if (i == countIndex) {
                    if (!Helpers::safe_full_string_to_integer(term, count)) {
                        xcerr << "Cannot convert " << term << " to integer" << std::endl;
                    }
                }
            }

            xstring token_store;
            xstring_view token;

            if (tokens.size() < termCount) {
                token = line;

            } else if (termCount == 1) {
                token = tokens[0];

            } else {
                token_store = Helpers::strings_join(tokens[0], XL(' '), tokens[1]);
                token = token_store;
            }

            bigrams.emplace(token, count);

            if (count < bigramCountMin) {
                bigramCountMin = count;
            }
        }

        return !bigrams.empty();
    }

    bool SymSpell::LoadDictionary(const std::string &corpus, int termIndex, int countIndex, xchar separatorChars) {

        xifstream corpusStream(corpus);
#ifdef UNICODE_SUPPORT
        std::locale utf8(std::locale(), new std::codecvt_utf8<wchar_t>);
        corpusStream.imbue(utf8);
#endif
        if (!corpusStream.is_open())
            return false;

        return LoadDictionary(corpusStream, termIndex, countIndex, separatorChars);
    }

    bool SymSpell::LoadDictionary(xifstream &corpusStream, int termIndex, int countIndex, xchar separatorChars) {
        SuggestionStage staging(16384);
        xstring line;
        const int minTerms = std::max(termIndex, countIndex);

        while (getline(corpusStream, line)) {
            int64_t count = 1;
            xstring_view token = line;
            size_t linePos = 0;
            xstring_view term;

            for (int i = 0; i <= minTerms && Helpers::split_line(line, separatorChars, linePos, term); ++i) {
                if (i == termIndex) {
                    token = term;
                }

                if (i == countIndex) {
                    if (!Helpers::safe_full_string_to_integer(term, count)) {
                        xcerr << "Cannot convert " << term << " to integer" << std::endl;
                    }
                }
            }

            CreateDictionaryEntry(token, count, staging);
        }

        CommitStaged(staging);
        return EntryCount() != 0;
    }

    bool SymSpell::CreateDictionary(const std::string &corpus) {
        xifstream corpusStream;
        corpusStream.open(corpus);
#ifdef UNICODE_SUPPORT
        std::locale utf8(std::locale(), new std::codecvt_utf8<wchar_t>);
        corpusStream.imbue(utf8);
#endif
        if (!corpusStream.is_open()) return false;

        return CreateDictionary(corpusStream);
    }

    bool SymSpell::CreateDictionary(xifstream &corpusStream) {
        xstring line;
        SuggestionStage staging(16384);

        while (getline(corpusStream, line)) {
            xstring key;
            size_t pos = 0;

            while (ParseWords(line, pos, key)) {
                CreateDictionaryEntry(key, 1, staging);
            }
        }

        CommitStaged(staging);
        return EntryCount() != 0;
    }

    void SymSpell::PurgeBelowThresholdWords() {
        belowThresholdWords.clear();
    }

    void SymSpell::CommitStaged(SuggestionStage &staging) {
        if (deletes.empty())
            deletes.reserve(staging.DeleteCount());

        staging.CommitTo(deletes);
    }

    std::vector<SuggestItem> SymSpell::Lookup(const xstring_view& input, Verbosity verbosity) const {
        return Lookup(input, verbosity, maxDictionaryEditDistance, false, false);
    }

    std::vector<SuggestItem> SymSpell::Lookup(const xstring_view& input, Verbosity verbosity, int maxEditDistance) const {
        return Lookup(input, verbosity, maxEditDistance, false, false);
    }

    std::vector<SuggestItem> SymSpell::Lookup(const xstring_view& input, Verbosity verbosity, int maxEditDistance, bool includeUnknown) const {
        return Lookup(input, verbosity, maxEditDistance, includeUnknown, false);
    }

    std::vector<SuggestItem>
    SymSpell::Lookup(const xstring_view& original_input, Verbosity verbosity, int maxEditDistance, bool includeUnknown,
                     bool transferCasing) const {
        if (deletes.empty()) return std::vector<SuggestItem> {}; // Dictionary is empty

        if (maxEditDistance > maxDictionaryEditDistance) throw std::invalid_argument("Distance too large");

        xstring lower_input;

        if (transferCasing) {
            Helpers::string_lower(original_input, lower_input);
        }

        const xstring_view& input = transferCasing ? lower_input : original_input;

        std::vector<SuggestItem> suggestions;
        const int inputLen = input.size();
        bool skip = (inputLen - maxEditDistance) > maxDictionaryWordLength;

        int64_t suggestionCount = 0;

        if (!skip) {
            auto wordsFinded = words.find(input);

            if (wordsFinded != words.end()) {
                suggestionCount = wordsFinded->second;
                suggestions.emplace_back(transferCasing ? original_input : input, 0, suggestionCount);
                if (verbosity != All) skip = true;
            }
        }

        if (maxEditDistance == 0) skip = true;

        if (!skip) {
            tsl::array_set<xchar> hashset1;
            tsl::array_set<xchar> hashset2;
            hashset2.insert(input);

            int maxEditDistance2 = maxEditDistance;
            std::deque<xstring> candidates;

            int inputPrefixLen = inputLen;
            if (inputPrefixLen > prefixLength) {
                inputPrefixLen = prefixLength;
                candidates.emplace_back(input.substr(0, inputPrefixLen));
            } else {
                candidates.emplace_back(input);
            }
            auto distanceComparer = EditDistance(distanceAlgorithm);

            while (!candidates.empty()) {
                const xstring candidate = std::move(candidates.front());
                candidates.pop_front();

                const int candidateLen = candidate.size();
                const int lengthDiff = inputPrefixLen - candidateLen;

                if (lengthDiff > maxEditDistance2) {
                    if (verbosity == Verbosity::All) continue;
                    break;
                }

                const auto deletes_found = deletes.find(GetstringHash(candidate));

                //read candidate entry from deletes' map
                if (deletes_found != deletes.end()) {
                    for (const words_it_t& suggestion_it : deletes_found.value()) {
                        const xstring& suggestion = suggestion_it->first;
                        const int suggestionLen = suggestion.size();
                        if (suggestion == input) continue;
                        if ((abs(suggestionLen - inputLen) >
                             maxEditDistance2) // input and sugg lengths diff > allowed/current best distance
                            || (suggestionLen <
                                candidateLen) // sugg must be for a different delete string, in same bin only because of hash collision
                            || (suggestionLen == candidateLen && suggestion !=
                                                                 candidate)) // if sugg len = delete len, then it either equals delete or is in same bin only because of hash collision
                            continue;
                        const int suggPrefixLen = std::min(suggestionLen, prefixLength);
                        if (suggPrefixLen > inputPrefixLen &&
                            (suggPrefixLen - candidateLen) > maxEditDistance2)
                            continue;

                        int distance = 0;
                        int min_len = 0;
                        if (candidateLen == 0) {
                            //suggestions which have no common chars with input (inputLen<=maxEditDistance && suggestionLen<=maxEditDistance)
                            distance = std::max(inputLen, suggestionLen);
                            const bool flag = hashset2.insert(suggestion).second;
                            if (distance > maxEditDistance2 || !flag) continue;
                        } else if (suggestionLen == 1) {
                            if (input.find(suggestion[0]) == xstring::npos)
                                distance = inputLen;
                            else
                                distance = inputLen - 1;

                            const bool flag = hashset2.insert(suggestion).second;
                            if (distance > maxEditDistance2 || !flag) continue;
                        } else if ((prefixLength - maxEditDistance == candidateLen)
                                   && (((min_len = std::min(inputLen, suggestionLen) - prefixLength) > 1)
                                       && (input.compare(
                                            inputLen + 1 - min_len, xstring::npos,
                                            suggestion, suggestionLen + 1 - min_len, xstring::npos
                                        ) != 0))
                                   ||
                                   ((min_len > 0) && (input[inputLen - min_len] != suggestion[suggestionLen - min_len])
                                    && ((input[inputLen - min_len - 1] != suggestion[suggestionLen - min_len])
                                        || (input[inputLen - min_len] != suggestion[suggestionLen - min_len - 1])))) {
                            continue;
                        } else {
                            if ((verbosity != All &&
                                 !DeleteInSuggestionPrefix(candidate, candidateLen, suggestion, suggestionLen))
                                || !hashset2.insert(suggestion).second)
                                continue;
                            distance = distanceComparer.Compare(input, suggestion, maxEditDistance2);
                            if (distance < 0) continue;
                        }

                        if (distance <= maxEditDistance2) {
                            suggestionCount = words.at(suggestion);

                            if (!suggestions.empty()) {
                                if (verbosity == Closest) {
                                    if (distance < maxEditDistance2) suggestions.clear();

                                } else if (verbosity == Top) {
                                    if (distance < maxEditDistance2 || suggestionCount > suggestions[0].count) {
                                        maxEditDistance2 = distance;
                                        suggestions[0] = SuggestItem(suggestion, distance, suggestionCount);
                                    }
                                    continue;
                                }
                            }

                            if (verbosity != All) maxEditDistance2 = distance;
                            suggestions.emplace_back(suggestion, distance, suggestionCount);
                        }
                    }//end foreach
                }//end if

                if ((lengthDiff < maxEditDistance) && (candidateLen <= prefixLength)) {
                    if (verbosity != All && lengthDiff >= maxEditDistance2) continue;
                    xstring del;

                    for (int i = 0; i < candidateLen; i++) {
                        del.assign(candidate);
                        del.erase(i, 1);

                        if (hashset1.insert(del).second) {
                            candidates.emplace_back(std::move(del));
                        }
                    }
                }
            }//end while

            if (suggestions.size() > 1)
                std::sort(suggestions.begin(), suggestions.end());

            if (transferCasing) {
                for (auto &suggestion: suggestions) {
                    suggestion.term = Helpers::transfer_casing_for_similar_text(original_input, suggestion.term);
                }
            }
        }
        if (includeUnknown && (suggestions.empty())) suggestions.emplace_back(input, maxEditDistance + 1, 0);
        return suggestions;
    }//end if

    bool SymSpell::DeleteInSuggestionPrefix(const xstring_view& deleteSugg, int deleteLen,
                                            const xstring_view& suggestion, int suggestionLen) const {
        if (deleteLen == 0) return true;
        if (prefixLength < suggestionLen) suggestionLen = prefixLength;
        int j = 0;
        for (int i = 0; i < deleteLen; i++) {
            xchar delChar = deleteSugg[i];
            while (j < suggestionLen && delChar != suggestion[j]) j++;
            if (j == suggestionLen) return false;
        }
        return true;
    }

    bool SymSpell::ParseWords(const xstring_view &text, size_t& pos, xstring& result) {
        xsmatch m;

        if (std::regex_search(text.data() + pos, text.data() + text.size(), m, wordsRegex)) {
            auto const sub_m = m[0];
            result = Helpers::string_lower(xstring_view(sub_m.first, sub_m.length()));
            pos = sub_m.second - text.data();
            return true;
        }

        return false;
    }

    void
    SymSpell::Edits(const xstring_view &word, int editDistance, tsl::array_set<xchar>& deleteWords) const {
        editDistance++;

        if (word.size() > 1) {
            xstring del;
            del.reserve(word.size());

            for (int i = 0; i < word.size(); i++) {
                del.assign(word);
                del.erase(i, 1);

                if (deleteWords.insert(del).second) {
                    if (editDistance < maxDictionaryEditDistance) Edits(del, editDistance, deleteWords);
                }
            }
        }
    }

    tsl::array_set<xchar> SymSpell::EditsPrefix(const xstring_view& key) const {
        tsl::array_set<xchar> m;

        if (key.size() <= maxDictionaryEditDistance) m.insert(XL(""));
        if (key.size() > prefixLength) {
            const xstring_view sub_key = key.substr(0, prefixLength);
            m.insert(sub_key);
            Edits(sub_key, 0, m);
        } else {
            m.insert(key);
            Edits(key, 0, m);
        }
        return m;
    }

    int SymSpell::GetstringHash(const xstring_view& s) const {
        int len = s.size();
        int lenMask = len;
        if (lenMask > 3) lenMask = 3;

        unsigned int hash = 2166136261;
        for (auto i = 0; i < len; i++) {
            hash ^= s[i];
            hash *= 16777619;
        }

        hash &= compactMask;
        hash |= (unsigned int) lenMask;
        return (int) hash;
    }

    std::vector<SuggestItem> SymSpell::LookupCompound(const xstring_view &input) const {
        return LookupCompound(input, maxDictionaryEditDistance, false);
    }

    std::vector<SuggestItem> SymSpell::LookupCompound(const xstring_view &input, int editDistanceMax) const {
        return LookupCompound(input, editDistanceMax, false);
    }

    std::vector<SuggestItem> SymSpell::LookupCompound(const xstring_view &input, int editDistanceMax, bool transferCasing) const {
        std::vector<SuggestItem> suggestions;     //suggestions for a single term
        std::vector<SuggestItem> suggestionParts; //1 line with separate parts
        auto distanceComparer = EditDistance(distanceAlgorithm);
        bool lastCombi = false;

        size_t inputPos = 0;
        xstring prevTermWord, termWordStore;

        for (int i = 0; ParseWords(input, inputPos, termWordStore); i++, prevTermWord = std::move(termWordStore)) {
            const xstring_view termWord = termWordStore;
            suggestions = Lookup(termWord, Top, editDistanceMax);

            if ((i > 0) && !lastCombi) {
                std::vector<SuggestItem> suggestionsCombi = Lookup(
                    Helpers::strings_join(prevTermWord, termWord), Top, editDistanceMax
                );

                if (!suggestionsCombi.empty()) {
                    SuggestItem& suggested = suggestionsCombi.front();
                    SuggestItem const& best1 = suggestionParts.back();
                    SuggestItem best2_store;

                    if (suggestions.empty()) {
                        best2_store = SuggestItem(
                            termWord, editDistanceMax + 1,
                            (long) ((double) 10 / pow((double) 10, (double) termWord.size()))
                        );
                    }

                    SuggestItem const& best2 = suggestions.empty() ? best2_store : suggestions[0];
                    const int best_distance_sum = best1.distance + best2.distance;

                    if ((best_distance_sum >= 0) && (
                        (suggested.distance + 1 < best_distance_sum) || (
                            (suggested.distance + 1 == best_distance_sum) && (
                                (double) suggested.count > (double) best1.count / (double) N * (double) best2.count
                            )
                        )
                    )) {
                        suggested.distance++;
                        suggestionParts.back() = std::move(suggested);
                        lastCombi = true;
                        goto nextTerm;
                    }
                }
            }

            lastCombi = false;

            if ((!suggestions.empty()) && ((suggestions[0].distance == 0) || (termWord.size() == 1))) {
                suggestionParts.push_back(suggestions[0]);
            } else {
                SuggestItem suggestionSplitBest;

                if (!suggestions.empty())
                    suggestionSplitBest = suggestions[0];

                if (termWord.size() > 1) {
                    for (int j = 1; j < termWord.size(); j++) {
                        const xstring_view part1 = termWord.substr(0, j);
                        const xstring_view part2 = termWord.substr(j);

                        std::vector<SuggestItem> suggestions1 = Lookup(part1, Top, editDistanceMax);

                        if (!suggestions1.empty()) {
                            std::vector<SuggestItem> suggestions2 = Lookup(part2, Top, editDistanceMax);

                            if (!suggestions2.empty()) {
                                SuggestItem suggestionSplit = SuggestItem(
                                    Helpers::strings_join(suggestions1[0].term, XL(' '), suggestions2[0].term),
                                    0, 0
                                );

                                const int compared_distance = distanceComparer.Compare(
                                    termWord, suggestionSplit.term, editDistanceMax
                                );

                                const int distance2 = compared_distance < 0 ? editDistanceMax + 1 : compared_distance;

                                if (suggestionSplitBest.count) {
                                    if (distance2 > suggestionSplitBest.distance) continue;
                                    if (distance2 < suggestionSplitBest.distance) suggestionSplitBest.count = 0;
                                }

                                suggestionSplit.distance = distance2;
                                auto bigram_it = bigrams.find(suggestionSplit.term);

                                if (bigram_it != bigrams.end()) {
                                    suggestionSplit.count = bigram_it.value();

                                    if (!suggestions.empty()) {
                                        if (Helpers::StringIsUnion(termWord, suggestions1[0].term, suggestions2[0].term)) {
                                            suggestionSplit.count = std::max(
                                                suggestionSplit.count, suggestions[0].count + 2
                                            );

                                        } else if (
                                            (suggestions1[0].term == suggestions[0].term) ||
                                            (suggestions2[0].term == suggestions[0].term
                                        )) {
                                            suggestionSplit.count = std::max(
                                                suggestionSplit.count, suggestions[0].count + 1
                                            );
                                        }

                                    } else if (Helpers::StringIsUnion(termWord, suggestions1[0].term, suggestions2[0].term)) {
                                        suggestionSplit.count = std::max(
                                            suggestionSplit.count,
                                            std::max(suggestions1[0].count, suggestions2[0].count) + 2
                                        );
                                    }

                                } else {
                                    suggestionSplit.count = std::min<int64_t>(
                                        bigramCountMin,
                                        (double) suggestions1[0].count / (double) N * (double) suggestions2[0].count
                                    );
                                }

                                if (suggestionSplitBest.count == 0 ||
                                    (suggestionSplit.count > suggestionSplitBest.count))
                                    suggestionSplitBest = std::move(suggestionSplit);
                            }
                        }
                    }

                    if (suggestionSplitBest.count) {
                        suggestionParts.push_back(suggestionSplitBest);
                    } else {
                        suggestionParts.emplace_back(
                            termWord,
                            editDistanceMax + 1,
                            (long) ((double) 10 / pow((double) 10, (double) termWord.size()))
                        );
                    }
                } else {
                    suggestionParts.emplace_back(
                        termWord,
                        editDistanceMax + 1,
                        (long) ((double) 10 / pow((double) 10, (double) termWord.size()))
                    );
                }
            }
            nextTerm:;
        }

        double count = N;
        xstring s;

        for (const SuggestItem &si : suggestionParts) {
            s.append(si.term);
            s.push_back(XL(' '));
            count *= (double) si.count / (double) N;
        }

        rtrim(s);

        if (transferCasing) {
            s = Helpers::transfer_casing_for_similar_text(input, s);
        }

        std::vector<SuggestItem> suggestionsLine;
        suggestionsLine.emplace_back(s, distanceComparer.Compare(input, s, MAXINT), (long) count);
        return suggestionsLine;
    }

    Info SymSpell::WordSegmentation(const xstring_view &input) const {
        return WordSegmentation(input, MaxDictionaryEditDistance(), maxDictionaryWordLength);
    }

    Info SymSpell::WordSegmentation(const xstring_view &input, int maxEditDistance) const {
        return WordSegmentation(input, maxEditDistance, maxDictionaryWordLength);
    }

    Info SymSpell::WordSegmentation(const xstring_view &input, int maxEditDistance, int maxSegmentationWordLength) const {
        // v6.7
        // normalize ligatures:
        // "scientific"
        // "scientiﬁc" "ﬁelds" "ﬁnal"
        // TODO: Figure out how to do the below utf-8 normalization in C++.
        // input = input.Normalize(System.Text.NormalizationForm.FormKC).Replace("\u002D", "");//.Replace("\uC2AD","");
        int arraySize = std::min(maxSegmentationWordLength, (int) input.size());
        std::vector<Info> compositions = std::vector<Info>(arraySize);
        int circularIndex = -1;

        for (int j = 0; j < input.size(); j++) {
            int imax = std::min<int>(input.size() - j, maxSegmentationWordLength);

            for (int i = 1; i <= imax; i++) {
                xstring part(input.substr(j, i));
                xstring topResult;

                int separatorLength = 0;
                int topEd = 0;
                double topProbabilityLog = 0;

                if (isxspace(part[0])) {
                    part = part.substr(1);

                } else {
                    separatorLength = 1;
                }

                topEd += part.size();
                part.erase(std::remove(part.begin(), part.end(), XL(' ')), part.end());
                topEd -= part.size();

                //v6.7
                //Lookup against the lowercase term
                std::vector<SuggestItem> results = Lookup(Helpers::string_lower(part), Top, maxEditDistance);

                if (!results.empty()) {
                    topResult = results[0].term;

                    //v6.7
                    //retain/preserve upper case
                    if (is_xupper(part[0])) {
                        topResult[0] = to_xupper(topResult[0]);
                    }

                    topEd += results[0].distance;
                    topProbabilityLog = log10((double) results[0].count / (double) N);

                } else {
                    topResult = part;
                    topEd += part.size();
                    topProbabilityLog = log10(10.0 / (N * pow(10.0, part.size())));
                }

                int destinationIndex = ((i + circularIndex) % arraySize);

                if (j == 0) {
                    compositions[destinationIndex] = Info(
                        std::move(part), std::move(topResult),
                        topEd, topProbabilityLog
                    );

                    continue;
                }

                auto circular_distance = compositions[circularIndex].getDistance();
                auto destination_distance = compositions[destinationIndex].getDistance();
                auto circular_probablity = compositions[circularIndex].getProbability();
                auto destination_probablity = compositions[destinationIndex].getProbability();

                if (
                    (i == maxSegmentationWordLength) || (
                        (
                            (circular_distance + topEd == destination_distance) ||
                            (circular_distance + separatorLength + topEd == destination_distance)
                        ) && (
                            destination_probablity < circular_probablity + topProbabilityLog
                        )
                    ) || (
                        circular_distance + separatorLength + topEd < destination_distance
                    )
                ) {
                    //v6.7
                    //keep punctuation or spostrophe adjacent to previous word
                    if (
                        ((topResult.size() == 1) && (is_xpunct(topResult[0]) > 0)) ||
                        ((topResult.size() == 2) && (topResult.rfind(XL("’"), 0) == 0))
                    ) {
                        compositions[destinationIndex] = Info(
                            Helpers::strings_join(compositions[circularIndex].getSegmented(), part),
                            Helpers::strings_join(compositions[circularIndex].getCorrected(), topResult),
                            circular_distance + topEd,
                            circular_probablity + topProbabilityLog
                        );

                    } else {
                        compositions[destinationIndex] = Info(
                            Helpers::strings_join(compositions[circularIndex].getSegmented(), XL(' '), part),
                            Helpers::strings_join(compositions[circularIndex].getCorrected(), XL(' '), topResult),
                            circular_distance + separatorLength + topEd,
                            circular_probablity + topProbabilityLog
                        );
                    }
                }
            }

            if (++circularIndex == arraySize) {
                circularIndex = 0;
            }
        }
        return compositions[circularIndex];
    }

    void SymSpell::to_stream (std::ostream& out) const {
        serializer ser(out);

        ser.serialize(serializedHeader.data(), serializedHeader.size());
        ser.serialize<size_t>(1);

        ser.serialize<size_t>(this->maxDictionaryEditDistance);
        ser.serialize<size_t>(this->prefixLength);
        ser.serialize<size_t>(this->countThreshold);
        ser.serialize<DistanceAlgorithm>(this->distanceAlgorithm);

        ser.serialize<words_map_t>(this->belowThresholdWords);
        ser.serialize<size_t>(this->words.size());

        size_t word_pos = 0;
        tsl::robin_map<xchar const*, size_t> words_pos;
        words_pos.reserve(this->words.size());

        for (auto it = this->words.begin(); it != this->words.end(); ++it) {
            ser.serialize<words_map_t::key_type>(it->first);
            ser.serialize<words_map_t::mapped_type>(it->second);
            words_pos.emplace_hint(words_pos.end(), it->first.data(), word_pos++);
        }

        ser.serialize<size_t>(this->deletes.size());
        ser.serialize<double>(this->deletes.max_load_factor());

        for (auto it = this->deletes.begin(); it != this->deletes.end(); ++it) {
            auto& deletes_vec = it.value();
            ser.serialize<deletes_map_t::key_type>(it.key());
            ser.serialize<size_t>(deletes_vec.size());

            for (auto& words_it : deletes_vec) {
                ser.serialize<size_t>(words_pos.at(words_it->first.data()));
            }
        }

        this->bigrams.serialize(ser);

        ser.serialize<size_t>(this->compactMask);
        ser.serialize<size_t>(this->maxDictionaryWordLength);
        ser.serialize<size_t>(this->bigramCountMin);
    }

    SymSpell SymSpell::from_stream (std::istream& in) {
        deserializer dse(in);

        std::string header(serializedHeader.size(), '\0');
        dse.deserialize(header.data(), serializedHeader.size());

        if (header != serializedHeader) {
            throw std::runtime_error("Invalid serialized header.");
        }

        if (dse.deserialize<size_t>() != 1) {
            throw std::runtime_error("Invalid serialized version.");
        }

        size_t const max_dist = dse.deserialize<size_t>();
        size_t const prefix_length = dse.deserialize<size_t>();
        size_t const count_threshold = dse.deserialize<size_t>();
        DistanceAlgorithm const dist_algo = dse.deserialize<DistanceAlgorithm>();

        SymSpell result(max_dist, prefix_length, count_threshold, 0, dist_algo);

        dse.deserialize<words_map_t>(result.belowThresholdWords);
        size_t const words_size = dse.deserialize<size_t>();

        std::vector<words_it_t> words_pos;
        words_pos.reserve(words_size);

        for (size_t i = 0; i < words_size; ++i) {
            auto key = dse.deserialize<words_map_t::key_type>();
            auto value = dse.deserialize<words_map_t::mapped_type>();
            auto ins_it = result.words.emplace_hint(result.words.end(), std::move(key), std::move(value));
            words_pos.emplace_back(ins_it);
        }

        size_t const deletes_size = dse.deserialize<size_t>();
        double const deletes_ml = dse.deserialize<double>();

        result.deletes.max_load_factor(deletes_ml);
        result.deletes.reserve(deletes_size);

        for (size_t i = 0; i < deletes_size; ++i) {
            auto key = dse.deserialize<deletes_map_t::key_type>();
            auto const vec_size = dse.deserialize<size_t>();
            auto& value = result.deletes.emplace_hint(result.deletes.end(), std::move(key), 0).value();
            value.reserve(vec_size);

            for (size_t j = 0; j < vec_size; ++j) {
                value.emplace_back(words_pos[dse.deserialize<size_t>()]);
            }
        }

        result.bigrams = bigram_map_t::deserialize(dse, true);

        result.compactMask = dse.deserialize<size_t>();
        result.maxDictionaryWordLength = dse.deserialize<size_t>();
        result.bigramCountMin = dse.deserialize<size_t>();

        return result;
    }
}

std::ostream& operator<< (std::ostream &out, symspellcpppy::SymSpell const &ssp) {
    out << "SymSpell(" <<
        "word_count=" << ssp.WordCount() << ", " <<
        "entry_count=" << ssp.EntryCount() << ", " <<
        "count_threshold=" << ssp.CountThreshold() << ", " <<
        "max_dictionary_edit_distance=" << ssp.MaxDictionaryEditDistance() << ", " <<
        "prefix_length=" << ssp.PrefixLength() << ", " <<
        "max_length=" << ssp.MaxLength() << ", " <<
        "count_threshold=" << ssp.CountThreshold() << ", " <<
        "distance_algorithm=" << ssp.GetDistanceAlgorithm() <<
    ')';

    return out;
}
