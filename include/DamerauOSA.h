//
// Created by vigi99 on 25/09/20.
//

#pragma once

#include "BaseDistance.h"
#include "BaseSimilarity.h"
#include "Helpers.h"
#include <vector>
#include <cmath>
#include <climits>
#include <stdexcept>
#include <numeric>

class DamerauOSA : public BaseDistance, BaseSimilarity {
private:
    std::vector<int> baseChar1Costs;
    std::vector<int> basePrevChar1Costs;

public:
    DamerauOSA() = default;

    explicit DamerauOSA(int expectedMaxstringLength) {
        if (expectedMaxstringLength <= 0) throw std::invalid_argument("expectedMaxstringLength must be larger than 0");
        baseChar1Costs = std::vector<int>(expectedMaxstringLength, 0);
        basePrevChar1Costs = std::vector<int>(expectedMaxstringLength, 0);
    }

    double Distance(const xstring_view& string1, const xstring_view& string2) override {
        if (string1.empty()) return string2.size();
        if (string2.empty()) return string1.size();

        const xstring_view& str1 = (string1.size() > string2.size()) ? string2 : string1;
        const xstring_view& str2 = (string1.size() > string2.size()) ? string1 : string2;

        int len1, len2, start;
        Helpers::PrefixSuffixPrep(str1, str2, len1, len2, start);
        if (len1 == 0) return len2;

        if (len2 > baseChar1Costs.size()) {
            baseChar1Costs = std::vector<int>(len2, 0);
            basePrevChar1Costs = std::vector<int>(len2, 0);
        }
        return Distance(str1, str2, len1, len2, start, baseChar1Costs, basePrevChar1Costs);
    }

    double Distance(const xstring_view& string1, const xstring_view& string2, double maxDistance) override {
        if (string1.empty() || string2.empty()) return Helpers::NullDistanceResults(string1, string2, maxDistance);
        if (maxDistance <= 0) return (string1 == string2) ? 0 : -1;
        maxDistance = ceil(maxDistance);
        int iMaxDistance = (maxDistance <= INT_MAX) ? (int) maxDistance : INT_MAX;

        const xstring_view& str1 = (string1.size() > string2.size()) ? string2 : string1;
        const xstring_view& str2 = (string1.size() > string2.size()) ? string1 : string2;

        if (str2.size() - str1.size() > iMaxDistance) return -1;

        int len1, len2, start;
        Helpers::PrefixSuffixPrep(str1, str2, len1, len2, start);
        if (len1 == 0) return (len2 <= iMaxDistance) ? len2 : -1;

        if (len2 > baseChar1Costs.size()) {
            baseChar1Costs = std::vector<int>(len2, 0);
            basePrevChar1Costs = std::vector<int>(len2, 0);
        }
        if (iMaxDistance < len2) {
            return Distance(str1, str2, len1, len2, start, iMaxDistance, baseChar1Costs, basePrevChar1Costs);
        }
        return Distance(str1, str2, len1, len2, start, baseChar1Costs, basePrevChar1Costs);
    }

    double Similarity(const xstring_view& string1, const xstring_view& string2) override {
        if (string1.empty()) return (string2.empty()) ? 1 : 0;
        if (string2.empty()) return 0;

        const xstring_view& str1 = (string1.size() > string2.size()) ? string2 : string1;
        const xstring_view& str2 = (string1.size() > string2.size()) ? string1 : string2;

        int len1, len2, start;
        Helpers::PrefixSuffixPrep(str1, str2, len1, len2, start);
        if (len1 == 0) return 1.0;

        if (len2 > baseChar1Costs.size()) {
            baseChar1Costs = std::vector<int>(len2, 0);
            basePrevChar1Costs = std::vector<int>(len2, 0);
        }
        return Helpers::ToSimilarity(Distance(str1, str2, len1, len2, start, baseChar1Costs, basePrevChar1Costs),
                                     str2.size());
    }

    double Similarity(const xstring_view& string1, const xstring_view& string2, double minSimilarity) override {
        if (minSimilarity < 0 || minSimilarity > 1)
            throw std::invalid_argument("minSimilarity must be in range 0 to 1.0");
        if (string1.empty() || string2.empty()) return Helpers::NullSimilarityResults(string1, string2, minSimilarity);

        const xstring_view& str1 = (string1.size() > string2.size()) ? string2 : string1;
        const xstring_view& str2 = (string1.size() > string2.size()) ? string1 : string2;

        int iMaxDistance = Helpers::ToDistance(minSimilarity, str2.size());
        if (str2.size() - str1.size() > iMaxDistance) return -1;
        if (iMaxDistance <= 0) return (str1 == str2) ? 1 : -1;

        int len1, len2, start;
        Helpers::PrefixSuffixPrep(str1, str2, len1, len2, start);
        if (len1 == 0) return 1.0;

        if (len2 > baseChar1Costs.size()) {
            baseChar1Costs = std::vector<int>(len2, 0);
            basePrevChar1Costs = std::vector<int>(len2, 0);
        }
        if (iMaxDistance < len2) {
            return Helpers::ToSimilarity(
                    Distance(str1, str2, len1, len2, start, iMaxDistance, baseChar1Costs, basePrevChar1Costs),
                    str2.size());
        }
        return Helpers::ToSimilarity(Distance(str1, str2, len1, len2, start, baseChar1Costs, basePrevChar1Costs),
                                     str2.size());
    }

    static int
    Distance(const xstring_view& string1, const xstring_view& string2, int len1, int len2, int start, std::vector<int>& char1Costs,
             std::vector<int>& prevChar1Costs) {
        std::iota(char1Costs.begin(), char1Costs.end(), 1);

        xchar char1 = XL(' ');
        int currentCost = 0;
        for (int i = 0; i < len1; ++i) {
            xchar prevChar1 = char1;
            char1 = string1[start + i];
            xchar char2 = XL(' ');
            int leftCharCost, aboveCharCost;
            leftCharCost = aboveCharCost = i;
            int nextTransCost = 0;
            for (int j = 0; j < len2; ++j) {
                int thisTransCost = nextTransCost;
                nextTransCost = prevChar1Costs[j];
                prevChar1Costs[j] = currentCost = leftCharCost; // cost of diagonal (substitution)
                leftCharCost = char1Costs[j];    // left now equals current cost (which will be diagonal at next iteration)
                xchar prevChar2 = char2;
                char2 = string2[start + j];
                if (char1 != char2) {
                    if (aboveCharCost < currentCost) currentCost = aboveCharCost; // deletion
                    if (leftCharCost < currentCost) currentCost = leftCharCost;   // insertion
                    ++currentCost;
                    if ((i != 0) && (j != 0)
                        && (char1 == prevChar2)
                        && (prevChar1 == char2)
                        && (thisTransCost + 1 < currentCost)) {
                        currentCost = thisTransCost + 1; // transposition
                    }
                }
                char1Costs[j] = aboveCharCost = currentCost;
            }
        }
        return currentCost;
    }

    static int Distance(const xstring_view& string1, const xstring_view& string2, int len1, int len2, int start, int maxDistance,
                        std::vector<int>& char1Costs, std::vector<int>& prevChar1Costs) {
        std::iota(char1Costs.begin(), char1Costs.begin() + maxDistance, 1);
        std::fill(char1Costs.begin() + maxDistance, char1Costs.end(), maxDistance + 1);

        int lenDiff = len2 - len1;
        int jStartOffset = maxDistance - lenDiff;
        int jStart = 0;
        int jEnd = maxDistance;
        xchar char1 = XL(' ');
        int currentCost = 0;
        for (int i = 0; i < len1; ++i) {
            xchar prevChar1 = char1;
            char1 = string1[start + i];
            xchar char2 = XL(' ');
            int leftCharCost, aboveCharCost;
            leftCharCost = aboveCharCost = i;
            int nextTransCost = 0;
            jStart += (i > jStartOffset) ? 1 : 0;
            jEnd += (jEnd < len2) ? 1 : 0;
            for (int j = jStart; j < jEnd; ++j) {
                int thisTransCost = nextTransCost;
                nextTransCost = prevChar1Costs[j];
                prevChar1Costs[j] = currentCost = leftCharCost; // cost on diagonal (substitution)
                leftCharCost = char1Costs[j];     // left now equals current cost (which will be diagonal at next iteration)
                xchar prevChar2 = char2;
                char2 = string2[start + j];
                if (char1 != char2) {
                    if (aboveCharCost < currentCost) currentCost = aboveCharCost; // deletion
                    if (leftCharCost < currentCost) currentCost = leftCharCost;   // insertion
                    ++currentCost;
                    if ((i != 0) && (j != 0)
                        && (char1 == prevChar2)
                        && (prevChar1 == char2)
                        && (thisTransCost + 1 < currentCost)) {
                        currentCost = thisTransCost + 1; // transposition
                    }
                }
                char1Costs[j] = aboveCharCost = currentCost;
            }
            if (char1Costs[i + lenDiff] > maxDistance) return -1;
        }
        return (currentCost <= maxDistance) ? currentCost : -1;
    }
};
