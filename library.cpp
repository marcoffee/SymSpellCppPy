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

    SymSpell::SymSpell(int _maxDictionaryEditDistance, int _prefixLength, int _countThreshold, int _initialCapacity,
                       unsigned char _compactLevel, DistanceAlgorithm _distanceAlgorithm) :
            maxDictionaryEditDistance(_maxDictionaryEditDistance),
            prefixLength(_prefixLength),
            countThreshold(_countThreshold),
            distanceAlgorithm(_distanceAlgorithm) {
        if (_initialCapacity < 0) throw std::invalid_argument("initial_capacity is too small.");
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

        words.max_load_factor(0.9);
        words.reserve(_initialCapacity);

        deletes.max_load_factor(0.9);
    }

    bool SymSpell::CreateDictionaryEntryCheck(const xstring &key, int64_t count) {
        if (count <= 0) {
            if (countThreshold > 0)
                return false; // no point doing anything if count is zero, as it can't change anything
            count = 0;
        }

        auto belowThresholdWordsFinded = belowThresholdWords.find(key);

        if (countThreshold > 1 && belowThresholdWordsFinded != belowThresholdWords.end()) {
            int64_t& countPrevious = belowThresholdWordsFinded.value();
            count = (MAXINT - countPrevious > count) ? countPrevious + count : MAXINT;

            if (count >= countThreshold) {
                belowThresholdWords.erase(belowThresholdWordsFinded);

            } else {
                countPrevious = count;
                return false;
            }
        } else {
            auto wordsFinded = words.find(key);

            if (wordsFinded != words.end()) {
                int64_t& countPrevious = wordsFinded.value();
                countPrevious = (MAXINT - countPrevious > count) ? countPrevious + count : MAXINT;
                return false;

            } else if (count < CountThreshold()) {
                belowThresholdWords[key] = count;
                return false;
            }
        }

        words.insert(key, count);

        if (key.size() > maxDictionaryWordLength)
            maxDictionaryWordLength = key.size();

        return true;
    }

    bool SymSpell::CreateDictionaryEntry(const xstring &key, int64_t count) {
        if (!CreateDictionaryEntryCheck(key, count)) {
            return false;
        }

        //create deletes
        auto edits = EditsPrefix(key);

        //store deletes
        for (auto it = edits.cbegin(); it != edits.cend(); ++it) {
            deletes.try_emplace(GetstringHash(it.key_sv()), 0).first.value().emplace_back(key);
        }

        return true;
    }

    bool SymSpell::CreateDictionaryEntry(const xstring &key, int64_t count, SuggestionStage &staging) {
        if (!CreateDictionaryEntryCheck(key, count)) {
            return false;
        }

        //create deletes
        auto edits = EditsPrefix(key);

        //stage deletes
        for (auto it = edits.cbegin(); it != edits.cend(); ++it) {
            staging.Add(GetstringHash(it.key_sv()), key);
        }

        return true;
    }

    bool SymSpell::DeleteDictionaryEntry(const xstring &key) {
        auto wordsFinded = words.find(key);
        if (wordsFinded != words.end()) {
            words.erase(wordsFinded);
            if (wordsFinded.key_size() == maxDictionaryWordLength) {
                int max_size = 0;
                for (auto it = words.begin(); it != words.end(); ++it) {
                    max_size = std::max(static_cast<int>(it.key_size()), max_size);
                }
                maxDictionaryWordLength = max_size;
            }
            auto edits = EditsPrefix(key);
            for (auto it = edits.begin(); it != edits.end(); ++it) {
                int deleteHash = GetstringHash(it.key_sv());
                auto deletesFinded = deletes.find(deleteHash);
                if (deletesFinded != deletes.end()) {
                    auto delete_vec = deletesFinded->second;
                    auto it = std::find(delete_vec.begin(), delete_vec.end(), key);
                    if (it < delete_vec.end()) delete_vec.erase(it);
                }
            }
            return true;
        }
        return false;
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
        xstring line;
        int linePartsLength = (separatorChars == DEFAULT_SEPARATOR_CHAR) ? 3 : 2;
        while (getline(corpusStream, line)) {
            std::vector<xstring> lineParts;
            xstringstream ss(line);
            xstring token;
            while (getline(ss, token, separatorChars))
                lineParts.push_back(token);
            xstring key;
            int64_t count;
            if (lineParts.size() >= linePartsLength) {
                key = (separatorChars == DEFAULT_SEPARATOR_CHAR) ? lineParts[termIndex] + XL(" ") +
                                                                   lineParts[termIndex + 1]
                                                                 : lineParts[termIndex];
                try {
                    count = stoll(lineParts[countIndex]);

                } catch (...) {
                    printf("Cannot convert %s to integer\n", lineParts[countIndex].c_str());
                }
            } else {
                key = line;
                count = 1;
            }

            bigrams.emplace(key, count);
            if (count < bigramCountMin) bigramCountMin = count;
        }

        if (bigrams.empty())
            return false;
        return true;
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
        int i = 0;
        int start, end;
        start = clock();
        while (getline(corpusStream, line)) {
            i++;
            std::vector<xstring> lineParts;
            xstringstream ss(line);
            xstring token;
            while (getline(ss, token, separatorChars))
                lineParts.push_back(token);
            if (lineParts.size() >= 2) {
                int64_t count = 1;
                try {
                    count = std::stoll(lineParts[countIndex]);
                } catch (const std::invalid_argument &) {
                    // Do nothing
                }
                CreateDictionaryEntry(lineParts[termIndex], count, staging);
            } else {
                CreateDictionaryEntry(line, 1, staging);
            }

        }
        CommitStaged(staging);
        if (EntryCount() == 0)
            return false;
        return true;
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
            for (const xstring &key : ParseWords(line)) {
                CreateDictionaryEntry(key, 1, staging);
            }

        }
        CommitStaged(staging);
        if (EntryCount() == 0)
            return false;
        return true;
    }

    void SymSpell::PurgeBelowThresholdWords() {
        belowThresholdWords.clear();
    }

    void SymSpell::CommitStaged(SuggestionStage &staging) {
        if (deletes.empty())
            deletes.reserve(staging.DeleteCount());

        staging.CommitTo(deletes);
    }

    std::vector<SuggestItem> SymSpell::Lookup(const xstring& input, Verbosity verbosity) const {
        return Lookup(input, verbosity, maxDictionaryEditDistance, false, false);
    }

    std::vector<SuggestItem> SymSpell::Lookup(const xstring& input, Verbosity verbosity, int maxEditDistance) const {
        return Lookup(input, verbosity, maxEditDistance, false, false);
    }

    std::vector<SuggestItem> SymSpell::Lookup(const xstring& input, Verbosity verbosity, int maxEditDistance, bool includeUnknown) const {
        return Lookup(input, verbosity, maxEditDistance, includeUnknown, false);
    }

    std::vector<SuggestItem>
    SymSpell::Lookup(const xstring& original_input, Verbosity verbosity, int maxEditDistance, bool includeUnknown,
                     bool transferCasing) const {
        if (deletes.empty()) return std::vector<SuggestItem> {}; // Dictionary is empty

        int skip = 0;
        if (maxEditDistance > maxDictionaryEditDistance) throw std::invalid_argument("Distance too large");

        xstring lower_input;

        if (transferCasing) {
            lower_input = Helpers::string_lower(original_input);
        }

        const xstring& input = transferCasing ? lower_input : original_input;

        std::vector<SuggestItem> suggestions;
        const int inputLen = input.size();
        if (inputLen - maxEditDistance > maxDictionaryWordLength) skip = 1;

        int64_t suggestionCount = 0;
        if (words.count(input) && !skip) {
            suggestionCount = words.at(input);
            suggestions.emplace_back(transferCasing ? original_input : input, 0, suggestionCount);
            if (verbosity != All) skip = 1;
        }

        if (maxEditDistance == 0) skip = 1;

        if (!skip) {
            tsl::array_set<xchar> hashset1;
            tsl::array_set<xchar> hashset2;
            hashset2.insert(input);

            int maxEditDistance2 = maxEditDistance;
            int candidatePointer = 0;
            std::deque<xstring> candidates;

            int inputPrefixLen = inputLen;
            if (inputPrefixLen > prefixLength) {
                inputPrefixLen = prefixLength;
                candidates.emplace_back(input.substr(0, inputPrefixLen));
            } else {
                candidates.emplace_back(input);
            }
            auto distanceComparer = EditDistance(distanceAlgorithm);
            while (candidatePointer < candidates.size()) {
                const xstring& candidate = candidates[candidatePointer++];
                const int candidateLen = candidate.size();
                const int lengthDiff = inputPrefixLen - candidateLen;

                if (lengthDiff > maxEditDistance2) {
                    if (verbosity == Verbosity::All) continue;
                    break;
                }

                const auto deletes_found = deletes.find(GetstringHash(candidate));

                //read candidate entry from deletes' map
                if (deletes_found != deletes.end()) {
                    for (const xstring& suggestion : deletes_found.value()) {
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

                    for (int i = 0; i < candidateLen; i++) {
                        xstring del(candidate);
                        del.erase(i, 1);

                        if (hashset1.insert(del).second) { candidates.push_back(del); }
                    }
                }
            }//end while

            if (suggestions.size() > 1)
                std::sort(suggestions.begin(), suggestions.end(), [](SuggestItem &l, SuggestItem &r) {
                    return l.CompareTo(r) < 0 ? 1 : 0;
                });

            if (transferCasing) {
                for (auto &suggestion: suggestions) {
                    suggestion.term = Helpers::transfer_casing_for_similar_text(original_input, suggestion.term);
                }
            }
        }
        if (includeUnknown && (suggestions.empty())) suggestions.emplace_back(input, maxEditDistance + 1, 0);
        return suggestions;
    }//end if

    bool SymSpell::DeleteInSuggestionPrefix(const xstring& deleteSugg, int deleteLen,
                                            const xstring &suggestion, int suggestionLen) const {
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

    std::vector<xstring> SymSpell::ParseWords(const xstring_view &text) {
        xsmatch m;
        std::vector<xstring> matches;
        xchar const* begin = text.data();
        xchar const* const end = begin + text.size();

        while (regex_search(begin, end, m, wordsRegex)) {
            auto const sub_m = m[0];
            matches.emplace_back(Helpers::string_lower(xstring_view(sub_m.first, sub_m.length())));
            begin = sub_m.second;
        }

        return matches;
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

    std::vector<SuggestItem> SymSpell::LookupCompound(const xstring &input) const {
        return LookupCompound(input, maxDictionaryEditDistance, false);
    }

    std::vector<SuggestItem> SymSpell::LookupCompound(const xstring &input, int editDistanceMax) const {
        return LookupCompound(input, editDistanceMax, false);
    }

    std::vector<SuggestItem> SymSpell::LookupCompound(const xstring &input, int editDistanceMax, bool transferCasing) const {
        std::vector<xstring> termList1 = ParseWords(input);

        std::vector<SuggestItem> suggestions;     //suggestions for a single term
        std::vector<SuggestItem> suggestionParts; //1 line with separate parts
        auto distanceComparer = EditDistance(distanceAlgorithm);

        bool lastCombi = false;
        for (int i = 0; i < termList1.size(); i++) {
            suggestions = Lookup(termList1[i], Top, editDistanceMax);

            if ((i > 0) && !lastCombi) {
                std::vector<SuggestItem> suggestionsCombi = Lookup(termList1[i - 1] + termList1[i], Top,
                                                                   editDistanceMax);

                if (!suggestionsCombi.empty()) {
                    SuggestItem const& best1 = suggestionParts[suggestionParts.size() - 1];
                    SuggestItem best2 = SuggestItem();
                    if (!suggestions.empty()) {
                        best2 = suggestions[0];
                    } else {
                        best2.term = termList1[i];
                        best2.distance = editDistanceMax + 1;
                        best2.count = (long) ((double) 10 / pow((double) 10, (double) best2.term.size())); // 0;
                    }

                    int distance1 = best1.distance + best2.distance;
                    if ((distance1 >= 0) && ((suggestionsCombi[0].distance + 1 < distance1) ||
                                             ((suggestionsCombi[0].distance + 1 == distance1) &&
                                              ((double) suggestionsCombi[0].count >
                                               (double) best1.count / (double) N * (double) best2.count)))) {
                        suggestionsCombi[0].distance++;
                        suggestionParts[suggestionParts.size() - 1] = suggestionsCombi[0];
                        lastCombi = true;
                        goto nextTerm;
                    }
                }
            }
            lastCombi = false;

            if ((!suggestions.empty()) && ((suggestions[0].distance == 0) || (termList1[i].size() == 1))) {
                suggestionParts.push_back(suggestions[0]);
            } else {
                SuggestItem suggestionSplitBest;

                if (!suggestions.empty()) suggestionSplitBest.set(suggestions[0]);

                if (termList1[i].size() > 1) {
                    for (int j = 1; j < termList1[i].size(); j++) {
                        const xstring part1 = termList1[i].substr(0, j);
                        const xstring part2 = termList1[i].substr(j);
                        SuggestItem suggestionSplit = SuggestItem();
                        std::vector<SuggestItem> suggestions1 = Lookup(part1, Top, editDistanceMax);
                        if (!suggestions1.empty()) {
                            std::vector<SuggestItem> suggestions2 = Lookup(part2, Top, editDistanceMax);
                            if (!suggestions2.empty()) {
                                suggestionSplit.term = suggestions1[0].term + XL(" ") + suggestions2[0].term;

                                int distance2 = distanceComparer.Compare(termList1[i], suggestionSplit.term,
                                                                         editDistanceMax);
                                if (distance2 < 0) distance2 = editDistanceMax + 1;

                                if (suggestionSplitBest.count) {
                                    if (distance2 > suggestionSplitBest.distance) continue;
                                    if (distance2 < suggestionSplitBest.distance) suggestionSplitBest.count = 0;
                                }

                                suggestionSplit.distance = distance2;
                                auto bigram_it = bigrams.find(suggestionSplit.term);

                                if (bigram_it != bigrams.end()) {
                                    suggestionSplit.count = bigram_it.value();
                                    if (!suggestions.empty()) {
                                        if ((suggestions1[0].term + suggestions2[0].term == termList1[i])) {
                                            suggestionSplit.count = std::max(suggestionSplit.count,
                                                                             suggestions[0].count + 2);
                                        } else if ((suggestions1[0].term == suggestions[0].term) ||
                                                   (suggestions2[0].term == suggestions[0].term)) {
                                            suggestionSplit.count = std::max(suggestionSplit.count,
                                                                             suggestions[0].count + 1);
                                        }
                                    } else if ((suggestions1[0].term + suggestions2[0].term == termList1[i])) {
                                        suggestionSplit.count = std::max(suggestionSplit.count,
                                                                         std::max(suggestions1[0].count,
                                                                                  suggestions2[0].count) +
                                                                         2);
                                    }

                                } else {
                                    suggestionSplit.count = std::min(bigramCountMin,
                                                                     (int64_t) ((double) suggestions1[0].count /
                                                                                (double) N *
                                                                                (double) suggestions2[0].count));
                                }

                                if (suggestionSplitBest.count == 0 ||
                                    (suggestionSplit.count > suggestionSplitBest.count))
                                    suggestionSplitBest.set(suggestionSplit);
                            }
                        }
                    }

                    if (suggestionSplitBest.count) {
                        suggestionParts.push_back(suggestionSplitBest);
                    } else {
                        suggestionParts.emplace_back(
                            termList1[i],
                            editDistanceMax + 1,
                            (long) ((double) 10 / pow((double) 10, (double) termList1[i].size()))
                        );
                    }
                } else {
                    suggestionParts.emplace_back(
                        termList1[i],
                        editDistanceMax + 1,
                        (long) ((double) 10 / pow((double) 10, (double) termList1[i].size()))
                    );
                }
            }
            nextTerm:;
        }

        double count = N;
        xstring s;
        for (const SuggestItem &si : suggestionParts) {
            s += si.term;
            s += XL(' ');
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

    Info SymSpell::WordSegmentation(const xstring &input) const {
        return WordSegmentation(input, MaxDictionaryEditDistance(), maxDictionaryWordLength);
    }

    Info SymSpell::WordSegmentation(const xstring &input, int maxEditDistance) const {
        return WordSegmentation(input, maxEditDistance, maxDictionaryWordLength);
    }

    Info SymSpell::WordSegmentation(const xstring &input, int maxEditDistance, int maxSegmentationWordLength) const {
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
            int imax = std::min((int) input.size() - j, maxSegmentationWordLength);
            for (int i = 1; i <= imax; i++) {
                xstring part = input.substr(j, i);
                int separatorLength = 0;
                int topEd = 0;
                double topProbabilityLog = 0;
                xstring topResult;

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
                auto partLower = Helpers::string_lower(part);
                std::vector<SuggestItem> results = Lookup(partLower, Top, maxEditDistance);
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
                    compositions[destinationIndex].set(part, topResult, topEd, topProbabilityLog);
                } else {
                    auto circular_distance = compositions[circularIndex].getDistance();
                    auto destination_distance = compositions[destinationIndex].getDistance();
                    auto circular_probablity = compositions[circularIndex].getProbability();
                    auto destination_probablity = compositions[destinationIndex].getProbability();

                    if ((i == maxSegmentationWordLength)
                        || (((circular_distance + topEd == destination_distance)
                            || (circular_distance + separatorLength + topEd == destination_distance))
                            && (destination_probablity < circular_probablity + topProbabilityLog))
                        || (circular_distance + separatorLength + topEd < destination_distance)) {
                        //v6.7
                        //keep punctuation or spostrophe adjacent to previous word
                        if (((topResult.size() == 1) && (is_xpunct(topResult[0]) > 0)) || ((topResult.size() == 2) &&
                                                                                        (topResult.rfind(XL("’"), 0) ==
                                                                                            0))) {
                            xstring seg = compositions[circularIndex].getSegmented() + part;
                            xstring correct = compositions[circularIndex].getCorrected() + topResult;
                            int d = circular_distance + topEd;
                            double prob = circular_probablity + topProbabilityLog;
                            compositions[destinationIndex].set(seg, correct, d, prob);
                        } else {
                            xstring seg = compositions[circularIndex].getSegmented() + XL(" ") + part;
                            xstring correct = compositions[circularIndex].getCorrected() + XL(" ") + topResult;
                            int d = circular_distance + separatorLength + topEd;
                            double prob = circular_probablity + topProbabilityLog;
                            compositions[destinationIndex].set(seg, correct, d, prob);
                        }
                    }
                }
            }
            circularIndex++;
            if (circularIndex == arraySize) circularIndex = 0;
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

        this->deletes.serialize(ser);
        this->words.serialize(ser);
        this->belowThresholdWords.serialize(ser);
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

        SymSpell result(max_dist, prefix_length, count_threshold, 0, 0, dist_algo);

        result.deletes = deletes_map_t::deserialize(dse, true);
        result.words = words_map_t::deserialize(dse, true);
        result.belowThresholdWords = words_map_t::deserialize(dse, true);
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
