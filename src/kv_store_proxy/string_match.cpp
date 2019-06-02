//
//  string_match.cpp
//  kv-store
//
//  Created by ziteng on 17/6/20.
//

#include "string_match.h"
#include "util.h"

/*
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Glob-style pattern matching
 */
int stringmatchlen(const char *pattern, int patternLen, const char *str, int strLen, int nocase)
{
    while(patternLen) {
        switch(pattern[0]) {
            case '*':
                while (pattern[1] == '*') {
                    pattern++;
                    patternLen--;
                }
                if (patternLen == 1)
                    return 1; /* match */
                while(strLen) {
                    if (stringmatchlen(pattern+1, patternLen-1, str, strLen, nocase))
                        return 1; /* match */
                    str++;
                    strLen--;
                }
                return 0; /* no match */
                break;
            case '?':
                if (strLen == 0)
                    return 0; /* no match */
                str++;
                strLen--;
                break;
            case '[':
            {
                int exclusion, match;
                
                pattern++;
                patternLen--;
                exclusion = pattern[0] == '^';
                if (exclusion) {
                    pattern++;
                    patternLen--;
                }
                match = 0;
                while(1) {
                    if (pattern[0] == '\\') {
                        pattern++;
                        patternLen--;
                        if (pattern[0] == str[0])
                            match = 1;
                    } else if (pattern[0] == ']') {
                        break;
                    } else if (patternLen == 0) {
                        pattern--;
                        patternLen++;
                        break;
                    } else if (pattern[1] == '-' && patternLen >= 3) {
                        int start = pattern[0];
                        int end = pattern[2];
                        int c = str[0];
                        if (start > end) {
                            int t = start;
                            start = end;
                            end = t;
                        }
                        if (nocase) {
                            start = tolower(start);
                            end = tolower(end);
                            c = tolower(c);
                        }
                        pattern += 2;
                        patternLen -= 2;
                        if (c >= start && c <= end)
                            match = 1;
                    } else {
                        if (!nocase) {
                            if (pattern[0] == str[0])
                                match = 1;
                        } else {
                            if (tolower((int)pattern[0]) == tolower((int)str[0]))
                                match = 1;
                        }
                    }
                    pattern++;
                    patternLen--;
                }
                if (exclusion)
                    match = !match;
                if (!match)
                    return 0; /* no match */
                str++;
                strLen--;
                break;
            }
            case '\\':
                if (patternLen >= 2) {
                    pattern++;
                    patternLen--;
                }
                /* fall through */
            default:
                if (!nocase) {
                    if (pattern[0] != str[0])
                        return 0; /* no match */
                } else {
                    if (tolower((int)pattern[0]) != tolower((int)str[0]))
                        return 0; /* no match */
                }
                str++;
                strLen--;
                break;
        }
        pattern++;
        patternLen--;
        if (strLen == 0) {
            while(*pattern == '*') {
                pattern++;
                patternLen--;
            }
            break;
        }
    }
    if (patternLen == 0 && strLen == 0)
        return 1;
    return 0;
}

int stringmatch(const string& pattern, const string& str, int nocase)
{
    return stringmatchlen(pattern.c_str(), (int)pattern.size(), str.c_str(), (int)str.size(), nocase);
}
