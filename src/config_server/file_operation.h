/*
 * file_operation.h
 *
 *  Created on: 2016-5-23
 *      Author: ziteng
 */

#ifndef __CS_FILE_OPERATION_H__
#define __CS_FILE_OPERATION_H__

#include "util.h"

bool load_bucket_tables();

bool save_bucket_table(const string& ns, uint32_t version, const map<uint16_t, string>& bucket_map,
                       uint16_t migrating_bucket_id, const string& migrating_server_addr);

void del_bucket_table(const string& ns);

#endif
