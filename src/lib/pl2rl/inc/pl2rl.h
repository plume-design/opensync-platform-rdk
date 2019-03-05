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

/*
 * pl2rl.h
 *
 * OpenSync Logger to RDK Logger Library
 */

#ifndef PL2RL_H_INCLUDED
#define PL2RL_H_INCLUDED

/*****************************************************************************/

// Abstract socket path
#define PL2RL_SOCKET_PATH       "\0/tmp/pl2rl.sock"

#define PL2RL_NAME_LEN          32

/*****************************************************************************/

// Message types
typedef enum
{
    PL2RL_MSG_TYPE_REGISTER     = 0,
    PL2RL_MSG_TYPE_LOG,
    PL2RL_MSG_TYPE_MAX
} pl2rl_msg_type_t;

// Message Header
typedef struct __attribute__((__packed__))
{
    uint16_t        msg_type;
    uint16_t        length;
} pl2rl_msg_hdr_t;

// Register Data
typedef struct __attribute__((__packed__))
{
    uint32_t        pid;
    char            name[PL2RL_NAME_LEN];
} pl2rl_msg_reg_data_t;

// Log Data
typedef struct __attribute__((__packed__))
{
    uint8_t         severity;
    uint8_t         module;
    uint16_t        text_len;
} pl2rl_msg_log_data_t;

// Full Msg
typedef struct __attribute__((__packed__))
{
    pl2rl_msg_hdr_t         hdr;
    union {
        pl2rl_msg_reg_data_t    reg;
        pl2rl_msg_log_data_t    log;
    } data;
} pl2rl_msg_t;

/*****************************************************************************/

extern bool     pl2rl_init(void);
extern void     pl2rl_log(logger_msg_t *lmsg);


#endif /* PL2RL_H_INCLUDED */
