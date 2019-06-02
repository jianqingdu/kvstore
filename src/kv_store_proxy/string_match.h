//
//  string_match.h
//  kv-store
//
//  Created by ziteng on 17/6/20.
//

#ifndef __STRING_MATCH_H__
#define __STRING_MATCH_H__

#include "util.h"

int stringmatchlen(const char *pattern, int patternLen, const char *str, int strLen, int nocase);

int stringmatch(const string& pattern, const string& str, int nocase);

#endif /* __STRING_MATCH_H__ */
