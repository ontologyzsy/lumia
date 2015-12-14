#!/bin/bash
while [[ -n "$1" ]]; do 
    case "$1" in 
        -hybrid) echo "deploy galaxy hybrid type";;
        -data_center) DATA_CENTER="$2"
        shift;;
        -disk_size) DISK_SIZE="$2"
        shift;;
        -bandwidth) BANDWIDTH="$2"
        shift;;
        -flag) FLAGS="$2"
        shift;;
        -ftp_path) FTP_PATH="$2"
        shift;;
        *)  echo "$1 is not an arg";;
    esac
    shift
done

babysitter /home/galaxy/agent/bin/galaxy-agent.conf stop >/dev/null 2>&1

CGROUP_ROOT=/cgroups
if [[ ! -d "$CGROUP_ROOT" ]]; then 
    mkdir -p $CGROUP_ROOT && mount -t tmpfs cgroup $CGROUP_ROOT
fi
if [[ ! -d "$CGROUP_ROOT/cpu" ]]; then 
    mkdir -p $CGROUP_ROOT/cpu && mount -t cgroup -ocpu none $CGROUP_ROOT/cpu 
fi
if [[ ! -d "$CGROUP_ROOT/memory" ]]; then 
    mkdir -p $CGROUP_ROOT/memory && mount -t cgroup -omemory none $CGROUP_ROOT/memory
fi
if [[ ! -d "$CGROUP_ROOT/cpuacct" ]]; then 
    mkdir -p $CGROUP_ROOT/cpuacct && mount -t cgroup -ocpuacct none $CGROUP_ROOT/cpuacct
fi
if [[ ! -d "$CGROUP_ROOT/freezer" ]]; then 
    mkdir -p $CGROUP_ROOT/freezer && mount -t cgroup -ofreezer none $CGROUP_ROOT/freezer
fi
if [[ ! -d "$CGROUP_ROOT/tcp_throt" ]]; then 
    mkdir -p $CGROUP_ROOT/tcp_throt && mount -t cgroup -otcp_throt none $CGROUP_ROOT/tcp_throt 
fi 

echo 0 > /proc/sys/kernel/printk 
GALAXY_HOME="/home/galaxy"
mkdir -p ${GALAXY_HOME} 
cd ${GALAXY_HOME} || exit 1 
df | grep ${GALAXY_HOME} 
if [[ $? -ne 0 ]]; then 
    home_dev=$(df | grep /home | head -n 1 | awk '{print $1}') 
    wget -qO ${GALAXY_HOME}/dd cq01-spi-shylock0.cq01:/home/spider/opbin/shylock/dd 
    chmod +x ${GALAXY_HOME}/dd 
    ${GALAXY_HOME}/dd if=${home_dev} of="/home/.FS_on_file_galaxy_agent" bs=1k count=$(( DISK_SIZE / 1000 )) io=10000000 /sbin/losetup /dev/loop0 /home/.FS_on_file_galaxy_agent
    /sbin/mkfs -t ext3 /dev/loop0 
    mount /dev/loop0 ${GALAXY_HOME} 
fi 
/usr/sbin/adduser galaxy >/dev/null 2>&1 
AGENT_IP=$(hostname -i) 
AGENT_HOME=${GALAXY_HOME}/agent 
mkdir -p $AGENT_HOME/work_dir 
mkdir -p $AGENT_HOME/gc_dir 
mkdir -p $AGENT_HOME/log && cd $AGENT_HOME && wget -qO tmp.tar.gz $FTP_PATH 
tar -zxvf tmp.tar.gz 
sed -i 's/--agent_mem_share=.*/--agent_mem_share=10000000000/' conf/galaxy.flag 
echo "--agent_ip=$AGENT_IP" >> conf/galaxy.flag 
echo "--nexus_root_path=/baidu/galaxy-$DATA_CENTER" >> conf/galaxy.flag 
host=$(hostname) 
host=${host%.baidu.com} 
cpu_cores=$(meta-query entity host ${host} -f cpuPhysicalCores | grep "${host}" | awk '{print $2}') 
cpu_cores=$((cpu_cores / 8)) 
if [[ cpu_cores -eq 0 ]]; then 
    exit 2
fi 
cpu_cores=$((cpu_cores * 2)) 
echo "--agent_millicores_share=${cpu_cores}000" >> conf/galaxy.flag 
echo "--agent_deploy_hybrid=true" >> conf/galaxy.flag 
echo "--agent_gc_timeout=600" >> conf/galaxy.flag 
echo "--send_bps_quota=$BANDWIDTH" >> conf/galaxy.flag 
echo "--recv_bps_quota=$BANDWIDTH" >> conf/galaxy.flag 
echo "--gce_support_subsystems=cpu,memory,cpuacct,freezer,tcp_throt" >> conf/galaxy.flag
df -h | grep ssd | awk '{print $6}' >> conf/mount_bind.template 
df -h | grep disk | awk '{print $6}' >> conf/mount_bind.template
sleep 1
babysitter /home/galaxy/agent/bin/galaxy-agent.conf start
