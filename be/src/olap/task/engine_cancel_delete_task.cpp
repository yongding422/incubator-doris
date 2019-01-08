// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "olap/task/engine_cancel_delete_task.h"

#include <list>

namespace doris {

using std::list;

EngineCancelDeleteTask::EngineCancelDeleteTask(const TCancelDeleteDataReq& request):_request(request) {

}

OLAPStatus EngineCancelDeleteTask::execute() {
    OLAPStatus res =  _cancel_delete();
    return res;
} // execute

OLAPStatus EngineCancelDeleteTask::_cancel_delete() {
    LOG(INFO) << "begin to process cancel delete."
              << "tablet=" << _request.tablet_id
              << ", version=" << _request.version;

    DorisMetrics::cancel_delete_requests_total.increment(1);

    OLAPStatus res = OLAP_SUCCESS;

    // 1. Get all tablets with same tablet_id
    list<TabletSharedPtr> table_list;
    res = TabletManager::instance()->get_tablets_by_id(_request.tablet_id, &table_list);
    if (res != OLAP_SUCCESS) {
        OLAP_LOG_WARNING("can't find tablet. [tablet=%ld]", _request.tablet_id);
        return OLAP_ERR_TABLE_NOT_FOUND;
    }

    // 2. Remove delete conditions from each tablet.
    DeleteConditionHandler cond_handler;
    for (TabletSharedPtr temp_tablet : table_list) {
        temp_tablet->obtain_header_wrlock();
        DelPredicateArray* delete_conditions = temp_tablet->mutable_delete_predicate();
        res = cond_handler.delete_cond(delete_conditions, _request.version, false);
        if (res != OLAP_SUCCESS) {
            temp_tablet->release_header_lock();
            OLAP_LOG_WARNING("cancel delete failed. [res=%d tablet=%s]",
                             res, temp_tablet->full_name().c_str());
            break;
        }

        res = temp_tablet->save_tablet_meta();
        if (res != OLAP_SUCCESS) {
            temp_tablet->release_header_lock();
            OLAP_LOG_WARNING("fail to save header. [res=%d tablet=%s]",
                             res, temp_tablet->full_name().c_str());
            break;
        }
        temp_tablet->release_header_lock();
    }

    // Show delete conditions in tablet header.
    for (TabletSharedPtr tablet : table_list) {
        tablet->obtain_header_rdlock();
        const DelPredicateArray& delete_conditions = tablet->delete_data_conditions();
        cond_handler.log_conds(tablet->full_name(), delete_conditions);
        tablet->release_header_lock();
    }

    LOG(INFO) << "finish to process cancel delete. res=" << res;
    return res;
} //_cancel_delete

} // doris