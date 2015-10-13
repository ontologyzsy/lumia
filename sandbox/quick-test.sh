#!/usr/bin/env sh
SANDBOX_HOME=`pwd`
tar -zxvf data.tar.gz
echo "--lumia_ctrl_host=0.0.0.0" >lumia.flag
echo "--lumia_ctrl_port=8081" >>lumia.flag
echo "--rms_api_http_host=http://api.rms.baidu.com" >>lumia.flag
echo "--ccs_api_http_host=http://jc.noah.baidu.com" >>lumia.flag
echo "--exec_job_check_interval=10000" >> lumia.flag
echo "--rms_api_check_job=http://rms.baidu.com/?r=interface/rest&handler=getCaseServerByListIdOrSn&list_id=" >> lumia.flag
echo "--nexus_servers=cp01-rdqa-dev400.cp01.baidu.com:8868,cp01-rdqa-dev400.cp01.baidu.com:8869,cp01-rdqa-dev400.cp01.baidu.com:8870,cp01-rdqa-dev400.cp01.baidu.com:8871,cp01-rdqa-dev400.cp01.baidu.com:8872" >>lumia.flag

sh start-lumia.sh
