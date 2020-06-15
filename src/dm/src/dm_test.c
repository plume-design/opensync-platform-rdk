/*
Copyright (c) 2017, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ev.h>

#include "log.h"
#include "os_socket.h"
#include "os_backtrace.h"
#include "ovsdb.h"
#include "schema.h"
#include "jansson.h"
#include "monitor.h"
#include "json_util.h"
#include "target.h"
#include "const.h"

#include "dm_test.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

#define DM_CFG_TBL_MAX 1
#define OVSDB_DEF_TABLE                 "Open_vSwitch"

#define REBOOT_SCRIPT_PATH "/usr/opensync/scripts/delayed-restart.sh"

/*
 * All the context information for DM
 */
typedef struct dm_ctx_s
{
    int monitor_id;
} dm_ctx_t;

static dm_ctx_t g_dm_ctx;

/*
 * The OBSDB tables that DM cares about
 */
static char *dm_cfg_tbl_names [] =
{
    "Wifi_Test_Config",
    "Wifi_Test_State",
};

static dm_cfg_table_parser_t * dm_cfg_table_handlers [DM_CFG_TBL_MAX] =
{
    dm_execute_command_config
};

static int dm_cfg_extract_table(json_t *value)
{
    int tbl_idx;
    dm_cfg_table_parser_t *handler = NULL;

    for (tbl_idx = 0; tbl_idx < DM_CFG_TBL_MAX; tbl_idx++)
    {
        json_t *tbl = NULL;
        if ((tbl = json_object_get(value, dm_cfg_tbl_names[tbl_idx])))
        {
            handler = dm_cfg_table_handlers[tbl_idx];
            handler(tbl);
        }
    }
    if (!handler)
    {
        LOG(NOTICE, "can't find table\n");
        return -1;
    }

    return 0;
}

static void dm_cfg_update_cb(int id, json_t *js, void * data)
{
    json_t *jparams = NULL;
    size_t index;
    json_t *value = NULL;

    if (id != g_dm_ctx.monitor_id)
    {
        LOG(NOTICE,"mon id mismatch!\n");
        return;
    }

    jparams = json_object_get(js, "params");
    json_array_foreach(jparams, index, value)
    {
        if (index == 0)
        {
            if (!json_is_integer(value) || id != json_integer_value(value))
            {
                goto _error_exit;
            }
        }
        else if (json_is_object(value))
        {
            dm_cfg_extract_table(value);
        }
        else
        {
            LOG(NOTICE,"can't find obj\n");
        }
    }

_error_exit:
    if (jparams) json_decref(jparams);
    if (value) json_decref(value);

    return;
}

static void dm_monitor_req_cb(int id, bool is_error, json_t *js, void * data)
{
    const char *key;
    json_t *value;
    int tbl_idx;
    dm_cfg_table_parser_t *handler;

    LOG(NOTICE,"monitor reply id %d \n", id);

    if (!json_is_object(js)) return;

    // Need to parse the existing table info
    json_object_foreach(js, key, value)
    {
        for (tbl_idx = 0; tbl_idx < DM_CFG_TBL_MAX; tbl_idx++)
        {
            if (!strcmp(key, dm_cfg_tbl_names[tbl_idx]))
            {
                handler = dm_cfg_table_handlers[tbl_idx];
                handler(value);
            }
        }
    }
}

int dm_config_monitor()
{
    int i, mon_id;
    int ret = -1;
    json_t *jparams = NULL;
    json_t *tbls = NULL;

    mon_id = ovsdb_register_update_cb(dm_cfg_update_cb, NULL);
    if (mon_id < 1)
    {
        LOG(NOTICE,"dm register update cb failed\n");
        return -1;
    }
    g_dm_ctx.monitor_id = mon_id;

    jparams = json_array();
    if (!jparams || json_array_append_new(jparams, json_string(OVSDB_DEF_TABLE)))
    {
        goto _error_exit;
    }
    if (json_array_append_new(jparams, json_integer(mon_id)))
    {
        goto _error_exit;
    }

    // Monitor all selects:
    // "tableName" : { "select" : {
    //                  "initial" : true,
    //                  "insert" : true,
    //                  "delete" : true,
    //                  "modify" : true
    //               }
    //             }
    tbls = json_object();
    for (i = 0; i < DM_CFG_TBL_MAX; i++)
    {
        json_t *tbl;

        tbl = json_pack("{s:{s:b, s:b, s:b, s:b}}", "select",
                "initial", true,
                "insert" , true,
                "delete" , true,
                "modify" , true);
        if (!tbl || json_object_set_new(tbls, dm_cfg_tbl_names[i], tbl))
        {
            goto _error_exit;
        }
    }

    if (json_array_append_new(jparams, tbls))
    {
        goto _error_exit;
    }

    // This routine steals ref of jparams and decref afterward,
    // no need to decref unless set_new fails
    ret = ovsdb_method_send(dm_monitor_req_cb, NULL, MT_MONITOR, jparams);

    return ret ? 0 : -1;

_error_exit:
    if (tbls) json_decref(tbls);
    if (jparams) json_decref(jparams);

    return -1;

}

json_t* dm_get_test_cfg_command_config(json_t *jtbl)
{
    json_t *jcfg = NULL;
    json_t *ival = NULL;
    void   *iter = NULL;
    const char *key;

    json_object_foreach(jtbl, key, ival)
    {
        if ((iter = json_object_iter_at(ival, "new")) != NULL)
        {
            break;
        }
        else if (json_object_iter_at(ival, "old"))
        {
            continue;
        }
    }

    jcfg = json_object_get(ival, "new");
    if (!jcfg)
    {
        LOG(NOTICE,"can't obtain cfg \n");
    }
    return jcfg;
}

/*
 * Wifi_Test_State table insert callback, called upon failed or successful
 * insert operation
 */
void insert_wifi_test_state_cb(int id, bool is_error, json_t *msg, void *data)
{
    (void)id;
    (void)is_error;
    (void)data;
    (void)msg;
    char *str;
    json_t *uuids = NULL;
    size_t index;
    json_t *value;
    json_t *oerr;

    str = json_dumps(msg, 0);
    LOG(NOTICE, "insert json response: %s\n", str);
    json_free(str);

    // Response itself is an array, extract it from response array
    if (1 == json_array_size(msg))
    {
        uuids = json_array_get(msg, 0);

        uuids = json_object_get(uuids, "uuid");

        if (NULL != uuids)
        {
            str = json_dumps (uuids, 0);
            LOG(NOTICE, "Wifi_Test_State::uuid=%s", str);
            json_free(str);
        }
    }
    else
    {
        // Array longer than 1 means an error occurred
        // Process the response and try to extract response error
        json_array_foreach(msg, index, value)
        {
            if (json_is_object(value))
            {
                oerr = json_object_get(value, "error");
                if (NULL != oerr)
                {
                    LOG(ERR, "OVSDB error::msg=%s", json_string_value(oerr));
                }

                oerr = json_object_get(value, "details");
                if (NULL != oerr)
                {
                    LOG(ERR, "OVSDB details::msg=%s", json_string_value(oerr));
                }
            }
        }
    }

}

/*
 * Try to fill in Wifi_Test_State table
 */
bool wifi_test_state_fill_entity (const char *p_test_id, const char *p_state)
{
    bool retval = false;
    struct schema_Wifi_Test_State s_wifi_test_state;

    memset(&s_wifi_test_state, 0, sizeof(struct schema_Wifi_Test_State));

    strcpy(s_wifi_test_state.test_id, p_test_id);
    strcpy(s_wifi_test_state.state, p_state);

    LOG(NOTICE, "Test State::test_id=%s|State=%s",
                                         s_wifi_test_state.test_id,
                                         s_wifi_test_state.state);

    retval = ovsdb_tran_call(insert_wifi_test_state_cb,
                             NULL,
                             "Wifi_Test_State",
                             OTR_INSERT,
                             NULL,
                             schema_Wifi_Test_State_to_json(&s_wifi_test_state, NULL));

    return retval;
}

int dm_execute_command_config (json_t *jtbl)
{
    json_t *jcfg;
    json_t *jtest_id;
    const char *path = NULL;
    const char *test_id = NULL;
    int ret = -1;

    if (!(jcfg = dm_get_test_cfg_command_config(jtbl)))
    {
        return -1;
    }

    // TEST ID
    jtest_id = json_object_get(jcfg, "test_id");
    if (jtest_id == NULL)
    {
        LOG(ERR, "DM TEST: Missing 'test_id' object.");
        return -1;
    }

    test_id = json_string_value(jtest_id);
    if (test_id == NULL)
    {
        LOG(ERR, "DM TEST: 'test_id' is not a string.");
        return -1;
    }
    LOG(DEBUG,"test_id: %s\n", test_id);

    if (strcmp(test_id, "reboot"))
    {
        LOGW("target supports only reboot command");
        return -1;
    }

    path = REBOOT_SCRIPT_PATH;

    // Verify that path is an executable
    if (access(path, X_OK) != 0)
    {
        LOG(ERR, "DM TEST: Path '%s' is not an executable.", path);
        return -1;
    }

    ret = system(REBOOT_SCRIPT_PATH);
    if (!WIFEXITED(ret) || WEXITSTATUS(ret) != 0)
    {
        LOGE("Failed to call OpenSync reboot script, ret = %d", ret);
        return -1;
    }

    wifi_test_state_fill_entity(test_id,"RUNNING");

    return ret;
}
