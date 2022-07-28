#pragma once

//#define UNICODE_SUPPORT

#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <locale>
#include <regex>
#include <iostream>
#include "cereal/types/unordered_map.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/memory.hpp"
#include "cereal/archives/binary.hpp"
#include "cereal/cereal.hpp"
#include "tsl/array-hash/array_set.h"
#include "tsl/array-hash/array_map.h"
#include "tsl/robin-map/robin_map.h"
#include "include/Defines.h"
#include "include/Helpers.h"
#include "include/EditDistance.h"

constexpr auto DEFAULT_SEPARATOR_CHAR = XL(' ');
constexpr auto DEFAULT_MAX_EDIT_DISTANCE = 2;
constexpr auto DEFAULT_PREFIX_LENGTH = 7;
constexpr auto DEFAULT_COUNT_THRESHOLD = 1;
constexpr auto DEFAULT_INITIAL_CAPACITY = 82765;
constexpr auto DEFAULT_COMPACT_LEVEL = 5;
constexpr auto DEFAULT_DISTANCE_ALGORITHM = DistanceAlgorithm::DamerauOSADistance;
constexpr auto MAXINT = LLONG_MAX;
constexpr auto MAXLONG = MAXINT;

// SymSpell supports compound splitting / decompounding of multi-word input strings with three cases:
// 1. mistakenly inserted space into a correct word led to two incorrect terms
// 2. mistakenly omitted space between two correct words led to one incorrect combined term
// 3. multiple independent input terms with/without spelling errors
namespace symspellcpppy {
    static inline void ltrim(xstring &s) {

        s.erase(s.begin(), find_if(s.begin(), s.end(), [](xchar ch) {
            return !isspace(ch);
        }));
    }

    static inline void rtrim(xstring &s) {
        s.erase(find_if(s.rbegin(), s.rend(), [](xchar ch) {
            return !isspace(ch);
        }).base(), s.end());
    }

    static inline void trim(xstring &s) {
        ltrim(s);
        rtrim(s);
    }

    class Info {
    private:

        xstring segmentedstring;
        xstring correctedstring;
        int distanceSum;
        double probabilityLogSum;
    public:
        void set(const xstring &seg, const xstring &cor, int d, double prob) {
            segmentedstring = seg;
            correctedstring = cor;
            distanceSum = d;
            probabilityLogSum = prob;
        };

        const xstring& getSegmented() const {
            return segmentedstring;
        };

        const xstring& getCorrected() const {
            return correctedstring;
        };

        int getDistance() const {
            return distanceSum;
        };

        double getProbability() const {
            return probabilityLogSum;
        };
    };

    /// <summary>Controls the closeness/quantity of returned spelling suggestions.</summary>
    enum Verbosity {
        /// <summary>Top suggestion with the highest term frequency of the suggestions of smallest edit distance found.</summary>
        Top,
        /// <summary>All suggestions of smallest edit distance found, suggestions ordered by term frequency.</summary>
        Closest,
        /// <summary>All suggestions within maxEditDistance, suggestions ordered by edit distance
        /// , then by term frequency (slower, no early termination).</summary>
        All
    };

    class SymSpell {
    protected:
        using deletes_map_t = tsl::robin_map<int, std::vector<xstring>>;
        using words_map_t = tsl::array_map<xchar, int64_t>;
        using bigram_map_t = tsl::array_map<xchar, long>;

        int maxDictionaryEditDistance;
        int prefixLength; //prefix length  5..7
        long countThreshold; //a threshold might be specified, when a term occurs so frequently in the corpus that it is considered a valid word for spelling correction
        int compactMask;
        DistanceAlgorithm distanceAlgorithm;
        int maxDictionaryWordLength; //maximum word length
        deletes_map_t deletes;
        words_map_t words;
        words_map_t belowThresholdWords;

        static const xregex wordsRegex;
        static constexpr std::string_view serializedHeader = "SymSpellCppPy";

        bool CreateDictionaryEntryCheck(const xstring &key, int64_t count);

    public:
        bigram_map_t bigrams;
        int64_t bigramCountMin = MAXLONG;

        int MaxDictionaryEditDistance() const;

        int PrefixLength() const;

        int MaxLength() const;

        long CountThreshold() const;

        DistanceAlgorithm GetDistanceAlgorithm() const;

        int WordCount() const;

        int EntryCount() const;

        /// <summary>Create a new instanc of SymSpell.</summary>
        /// <remarks>Specifying ann accurate initialCapacity is not essential,
        /// but it can help speed up processing by alleviating the need for
        /// data restructuring as the size grows.</remarks>
        /// <param name="initialCapacity">The expected number of words in dictionary.</param>
        /// <param name="maxDictionaryEditDistance">Maximum edit distance for doing lookups.</param>
        /// <param name="prefixLength">The length of word prefixes used for spell checking..</param>
        /// <param name="countThreshold">The minimum frequency count for dictionary words to be considered correct spellings.</param>
        /// <param name="compactLevel">Degree of favoring lower memory use over speed (0=fastest,most memory, 16=slowest,least memory).</param>
        explicit SymSpell(int maxDictionaryEditDistance = DEFAULT_MAX_EDIT_DISTANCE,
                          int prefixLength = DEFAULT_PREFIX_LENGTH, int countThreshold = DEFAULT_COUNT_THRESHOLD,
                          int initialCapacity = DEFAULT_INITIAL_CAPACITY,
                          unsigned char compactLevel = DEFAULT_COMPACT_LEVEL,
                          DistanceAlgorithm distanceAlgorithm = DEFAULT_DISTANCE_ALGORITHM);

        bool CreateDictionaryEntry(const xstring &key, int64_t count);
        bool CreateDictionaryEntry(const xstring &key, int64_t count, SuggestionStage &staging);

        bool DeleteDictionaryEntry(const xstring &key);

        /// <summary>Load multiple dictionary entries from a file of word/frequency count pairs</summary>
        /// <remarks>Merges with any dictionary data already loaded.</remarks>
        /// <param name="corpus">The path+filename of the file.</param>
        /// <param name="termIndex">The column position of the word.</param>
        /// <param name="countIndex">The column position of the frequency count.</param>
        /// <param name="separatorChars">Separator characters between term(s) and count.</param>
        /// <returns>True if file loaded, or false if file not found.</returns>
        bool LoadBigramDictionary(const std::string &corpus, int termIndex, int countIndex,
                                  xchar separatorChars = DEFAULT_SEPARATOR_CHAR);

        bool LoadBigramDictionary(xifstream &corpusStream, int termIndex, int countIndex,
                                  xchar separatorChars = DEFAULT_SEPARATOR_CHAR);

        /// <summary>Load multiple dictionary entries from a file of word/frequency count pairs</summary>
        /// <remarks>Merges with any dictionary data already loaded.</remarks>
        /// <param name="corpus">The path+filename of the file.</param>
        /// <param name="termIndex">The column position of the word.</param>
        /// <param name="countIndex">The column position of the frequency count.</param>
        /// <param name="separatorChars">Separator characters between term(s) and count.</param>
        /// <returns>True if file loaded, or false if file not found.</returns>
        bool LoadDictionary(const std::string &corpus, int termIndex, int countIndex,
                            xchar separatorChars = DEFAULT_SEPARATOR_CHAR);

        bool LoadDictionary(xifstream &corpusStream, int termIndex, int countIndex,
                            xchar separatorChars = DEFAULT_SEPARATOR_CHAR);

        /// <summary>Load multiple dictionary words from a file containing plain text.</summary>
        /// <remarks>Merges with any dictionary data already loaded.</remarks>
        /// <param name="corpus">The path+filename of the file.</param>
        /// <returns>True if file loaded, or false if file not found.</returns>
        bool CreateDictionary(const std::string &corpus);

        bool CreateDictionary(xifstream &corpusStream);

        /// <summary>Remove all below threshold words from the dictionary.</summary>
        /// <remarks>This can be used to reduce memory consumption after populating the dictionary from
        /// a corpus using CreateDictionary.</remarks>
        void PurgeBelowThresholdWords();

        void CommitStaged(SuggestionStage &staging);

        /// <summary>Find suggested spellings for a given input word, using the maximum
        /// edit distance specified during construction of the SymSpell dictionary.</summary>
        /// <param name="input">The word being spell checked.</param>
        /// <param name="verbosity">The value controlling the quantity/closeness of the retuned suggestions.</param>
        /// <returns>A List of SuggestItem object representing suggested correct spellings for the input word,
        /// sorted by edit distance, and secondarily by count frequency.</returns>
        std::vector<SuggestItem> Lookup(const xstring& input, Verbosity verbosity) const;

        /// <summary>Find suggested spellings for a given input word, using the maximum
        /// edit distance specified during construction of the SymSpell dictionary.</summary>
        /// <param name="input">The word being spell checked.</param>
        /// <param name="verbosity">The value controlling the quantity/closeness of the retuned suggestions.</param>
        /// <param name="maxEditDistance">The maximum edit distance between input and suggested words.</param>
        /// <returns>A List of SuggestItem object representing suggested correct spellings for the input word,
        /// sorted by edit distance, and secondarily by count frequency.</returns>
        std::vector<SuggestItem> Lookup(const xstring& input, Verbosity verbosity, int maxEditDistance) const;

        /// <summary>Find suggested spellings for a given input word.</summary>
        /// <param name="input">The word being spell checked.</param>
        /// <param name="verbosity">The value controlling the quantity/closeness of the retuned suggestions.</param>
        /// <param name="maxEditDistance">The maximum edit distance between input and suggested words.</param>
        /// <param name="includeUnknown">Include input word in suggestions, if no words within edit distance found.</param>
        /// <returns>A List of SuggestItem object representing suggested correct spellings for the input word,
        /// sorted by edit distance, and secondarily by count frequency.</returns>
        std::vector<SuggestItem> Lookup(const xstring& input, Verbosity verbosity, int maxEditDistance, bool includeUnknown) const;

        /// <summary>Find suggested spellings for a given input word.</summary>
        /// <param name="input">The word being spell checked.</param>
        /// <param name="verbosity">The value controlling the quantity/closeness of the retuned suggestions.</param>
        /// <param name="maxEditDistance">The maximum edit distance between input and suggested words.</param>
        /// <param name="includeUnknown">Include input word in suggestions, if no words within edit distance found.</param>
        /// <param name="transfer_casing"> Lower case the word or not
        /// <returns>A List of SuggestItem object representing suggested correct spellings for the input word,
        /// sorted by edit distance, and secondarily by count frequency.</returns>
        std::vector<SuggestItem> Lookup(const xstring& input, Verbosity verbosity, int maxEditDistance, bool includeUnknown, bool transferCasing) const;

    private:
        bool
        DeleteInSuggestionPrefix(const xstring& deleteSugg, int deleteLen, const xstring &suggestion, int suggestionLen) const;

        static std::vector<xstring> ParseWords(const xstring &text);

        void
        Edits(const xstring &word, int editDistance, tsl::array_set<xchar>& deleteWords) const;

        tsl::array_set<xchar> EditsPrefix(const xstring& key) const;

        int GetstringHash(const xstring_view& s) const;

    public:
        //######################

        //LookupCompound supports compound aware automatic spelling correction of multi-word input strings with three cases:
        //1. mistakenly inserted space into a correct word led to two incorrect terms
        //2. mistakenly omitted space between two correct words led to one incorrect combined term
        //3. multiple independent input terms with/without spelling errors

        /// <summary>Find suggested spellings for a multi-word input string (supports word splitting/merging).</summary>
        /// <param name="input">The string being spell checked.</param>
        /// <returns>A List of SuggestItem object representing suggested correct spellings for the input string.</returns>
        std::vector<SuggestItem> LookupCompound(const xstring &input) const;

        /// <summary>Find suggested spellings for a multi-word input string (supports word splitting/merging).</summary>
        /// <param name="input">The string being spell checked.</param>
        /// <param name="maxEditDistance">The maximum edit distance between input and suggested words.</param>
        /// <returns>A List of SuggestItem object representing suggested correct spellings for the input string.</returns>
        std::vector<SuggestItem> LookupCompound(const xstring &input, int editDistanceMax) const;

        /// <summary>Find suggested spellings for a multi-word input string (supports word splitting/merging).</summary>
        /// <param name="input">The string being spell checked.</param>
        /// <param name="maxEditDistance">The maximum edit distance between input and suggested words.</param>
        /// <returns>A List of SuggestItem object representing suggested correct spellings for the input string.</returns>
        std::vector<SuggestItem> LookupCompound(const xstring &input, int editDistanceMax, bool transferCasing) const;

        //######

        //WordSegmentation divides a string into words by inserting missing spaces at the appropriate positions
        //misspelled words are corrected and do not affect segmentation
        //existing spaces are allowed and considered for optimum segmentation

        //SymSpell.WordSegmentation uses a novel approach *without* recursion.
        //https://medium.com/@wolfgarbe/fast-word-segmentation-for-noisy-text-2c2c41f9e8da
        //While each string of length n can be segmentend in 2^n−1 possible compositions https://en.wikipedia.org/wiki/Composition_(combinatorics)
        //SymSpell.WordSegmentation has a linear runtime O(n) to find the optimum composition

        //number of all words in the corpus used to generate the frequency dictionary
        //this is used to calculate the word occurrence probability p from word counts c : p=c/N
        //N equals the sum of all counts c in the dictionary only if the dictionary is complete, but not if the dictionary is truncated or filtered
        static const int64_t N = 1024908267229L;

        /// <summary>Find suggested spellings for a multi-word input string (supports word splitting/merging).</summary>
        /// <param name="input">The string being spell checked.</param>
        /// <returns>The word segmented string,
        /// the word segmented and spelling corrected string,
        /// the Edit distance sum between input string and corrected string,
        /// the Sum of word occurrence probabilities in log scale (a measure of how common and probable the corrected segmentation is).</returns>
        Info WordSegmentation(const xstring &input) const;

        /// <summary>Find suggested spellings for a multi-word input string (supports word splitting/merging).</summary>
        /// <param name="input">The string being spell checked.</param>
        /// <param name="maxEditDistance">The maximum edit distance between input and corrected words
        /// (0=no correction/segmentation only).</param>
        /// <returns>The word segmented string,
        /// the word segmented and spelling corrected string,
        /// the Edit distance sum between input string and corrected string,
        /// the Sum of word occurrence probabilities in log scale (a measure of how common and probable the corrected segmentation is).</returns>
        Info WordSegmentation(const xstring &input, int maxEditDistance) const;

        /// <summary>Find suggested spellings for a multi-word input string (supports word splitting/merging).</summary>
        /// <param name="input">The string being spell checked.</param>
        /// <param name="maxSegmentationWordLength">The maximum word length that should be considered.</param>
        /// <param name="maxEditDistance">The maximum edit distance between input and corrected words
        /// (0=no correction/segmentation only).</param>
        /// <returns>The word segmented string,
        /// the word segmented and spelling corrected string,
        /// the Edit distance sum between input string and corrected string,
        /// the Sum of word occurrence probabilities in log scale (a measure of how common and probable the corrected segmentation is).</returns>
        Info WordSegmentation(const xstring &input, int maxEditDistance, int maxSegmentationWordLength) const;

        struct serializer {
            std::ostream& data;
            serializer (std::ostream& data) : data(data) {}

            template <typename U>
            void serialize (U const* value, size_t const& size) {
                if constexpr (std::is_arithmetic_v<U>) {
                    this->data.write(reinterpret_cast<char const*>(value), size * sizeof(U));

                } else {
                    for (size_t i = 0; i < size; ++i) {
                        this->serialize<U>(value[i]);
                    }
                }
            }

            template <typename U>
            void serialize (U const& value) {
                if constexpr (std::is_arithmetic_v<U> || std::is_enum_v<U>) {
                    this->data.write(reinterpret_cast<char const*>(&value), sizeof(U));

                } else if constexpr (is_std_vector<U>::value || is_std_string<U>::value) {
                    this->serialize<size_t>(value.size());
                    this->serialize<typename U::value_type>(value.data(), value.size());

                } else if constexpr (is_std_pair<U>::value) {
                    this->serialize(value.first);
                    this->serialize(value.second);

                } else {
                    throw std::runtime_error(typeid(U).name());
                }
            }

            template <typename U>
            void operator() (U const& value) { this->serialize<U>(value); }

            template <typename U>
            void operator() (U const* value, size_t const& size) { this->serialize<U>(value, size); }
        };

        struct deserializer {
            std::istream& data;
            deserializer (std::istream& data) : data(data) {}

            template <typename U>
            U deserialize (void) {
                U result{};

                if constexpr (std::is_arithmetic_v<U> || std::is_enum_v<U>) {
                    this->data.read(reinterpret_cast<char*>(&result), sizeof(U));

                } else if constexpr (is_std_vector<U>::value || is_std_string<U>::value) {
                    result.resize(this->deserialize<size_t>());
                    this->deserialize<typename U::value_type>(result.data(), result.size());

                } else if constexpr (is_std_pair<U>::value) {
                    result.first = this->deserialize<typeof(result.first)>();
                    result.second = this->deserialize<typeof(result.second)>();

                } else {
                    throw std::runtime_error(typeid(U).name());
                }

                return result;
            }

            template <typename U>
            void deserialize (U* out, size_t const& size) {
                if constexpr (std::is_arithmetic_v<U>) {
                    this->data.read(reinterpret_cast<char*>(out), size * sizeof(U));

                } else {
                    for (size_t i = 0; i < size; ++i) {
                        out[i] = this->deserialize<U>();
                    }
                }
            }

            template <typename U>
            U operator() () { return this->deserialize<U>(); }

            template <typename U>
            void operator() (U* out, size_t const& size) { this->deserialize<U>(out, size); }
        };

        void to_stream (std::ostream& out) const;
        static SymSpell from_stream (std::istream& in);

        template<class Archive>
        void save (Archive &ar) const {
            auto legacy_deletes = std::make_shared<std::unordered_map<int, std::vector<xstring>>>();
            std::unordered_map<xstring, int64_t> legacy_words;

            legacy_deletes->reserve(deletes.size());

            for (auto it = deletes.begin(); it != deletes.end(); ++it) {
                legacy_deletes->emplace_hint(legacy_deletes->end(), it.key(), it.value());
            }

            legacy_words.reserve(words.size());

            for (auto it = words.begin(); it != words.end(); ++it) {
                legacy_words.emplace_hint(legacy_words.end(), it.key_sv(), it.value());
            }

            ar(legacy_deletes, legacy_words, maxDictionaryWordLength);
        }

        template<class Archive>
        void load (Archive &ar) {
            std::shared_ptr<std::unordered_map<int, std::vector<xstring>>> legacy_deletes;
            std::unordered_map<xstring, int64_t> legacy_words;

            ar(legacy_deletes, legacy_words, maxDictionaryWordLength);

            deletes = deletes_map_t(legacy_deletes->begin(), legacy_deletes->end());
            words = words_map_t(legacy_words.begin(), legacy_words.end());
        }
    };
}

std::ostream& operator<< (std::ostream&, symspellcpppy::SymSpell const&);
