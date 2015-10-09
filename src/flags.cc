// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gflags/gflags.h>
DEFINE_string(lumia_ctrl_port, "8080", "lumia controller port");
DEFINE_string(minion_dict, "dict/minion.pb.dict", "minion dict");
DEFINE_string(scripts_dir, "scripts", "scripts dir");

DEFINE_string(rms_api_http_host, "xxx", "rms api http host");
DEFINE_string(ccs_api_http_host, "xxx", "ccs api http host");

DEFINE_string(rms_token, "b4631c7cb697d9ee7080126b18dd09abcfad79eb", "rms token");
DEFINE_string(rms_app_key, "112", "rms app key");
DEFINE_string(rms_auth_user, "wangtaize", "rms auth user");
DEFINE_int32(exec_job_check_interval, 2000, "exec job check interval");
