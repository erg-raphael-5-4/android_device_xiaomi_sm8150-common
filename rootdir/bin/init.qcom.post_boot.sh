#! /vendor/bin/sh

# Copyright (c) 2012-2013, 2016-2020, The Linux Foundation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of The Linux Foundation nor
#       the names of its contributors may be used to endorse or promote
#       products derived from this software without specific prior written
#       permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#


# Boot-time CPU, scheduler, bus and memory tuning for msmnile (SM8150).
#
# Derived from the msmnile path of Qualcomm's generic init.qcom.post_boot.sh.
# Only what applies to this 5.4 kernel is kept: writes to nodes that don't
# exist here (cpu_boost module parameters, lowmemorykiller, process_reclaim,
# soc:qcom,*-l3-lat) are gone, and each write below was checked on device.
# Memlat/L3 governors are set by init.qti.dcvs.sh; L3 and memory-latency
# floors and all runtime boosts come from the power HAL (powerhint.json).

write() {
    [ -e "$1" ] && echo "$2" > "$1"
}

configure_zram() {
    MemTotalStr=$(grep MemTotal /proc/meminfo)
    MemTotal=${MemTotalStr:16:8}
    # Half of RAM, at most 4 GiB.
    let zRamSizeMB="( ( $MemTotal / 1048576 ) + 1 ) * 512"
    [ $zRamSizeMB -gt 4096 ] && zRamSizeMB=4096

    [ -f /sys/block/zram0/disksize ] || return
    # lz4 costs noticeably less CPU per page than lzo-rle to compress and
    # decompress, for a slightly lower ratio.
    write /sys/block/zram0/comp_algorithm lz4
    write /sys/block/zram0/use_dedup 1
    echo ${zRamSizeMB}M > /sys/block/zram0/disksize
    # zsmalloc debug tracking costs memory when SLAB_STORE_USER is on.
    write /sys/kernel/slab/zs_handle/store_user 0
    write /sys/kernel/slab/zspage/store_user 0
    mkswap /dev/block/zram0
    swapon /dev/block/zram0 -p 32758
}

configure_memory() {
    configure_zram
    write /proc/sys/vm/swappiness 100
    write /proc/sys/vm/watermark_scale_factor 10
    write /proc/sys/vm/reap_mem_on_sigkill 1
    for ra in /sys/block/dm-*/queue/read_ahead_kb; do
        write $ra 512
    done
}

# Core control: keep two gold cores online, let gold+ go offline, never
# offline silver.
write /sys/devices/system/cpu/cpu4/core_ctl/min_cpus 2
write /sys/devices/system/cpu/cpu4/core_ctl/busy_up_thres 60
write /sys/devices/system/cpu/cpu4/core_ctl/busy_down_thres 30
write /sys/devices/system/cpu/cpu4/core_ctl/offline_delay_ms 100
write /sys/devices/system/cpu/cpu4/core_ctl/task_thres 3
write /sys/devices/system/cpu/cpu7/core_ctl/min_cpus 0
write /sys/devices/system/cpu/cpu7/core_ctl/busy_up_thres 60
write /sys/devices/system/cpu/cpu7/core_ctl/busy_down_thres 30
write /sys/devices/system/cpu/cpu7/core_ctl/offline_delay_ms 100
write /sys/devices/system/cpu/cpu7/core_ctl/task_thres 1
write /sys/devices/system/cpu/cpu7/core_ctl/nr_prev_assist_thresh 1
write /sys/devices/system/cpu/cpu0/core_ctl/enable 0

# WALT placement: migrate up late, down early, so work stays on silver
# unless it really needs a bigger core.
write /proc/sys/kernel/sched_upmigrate "95 95"
write /proc/sys/kernel/sched_downmigrate "85 85"
write /proc/sys/kernel/sched_group_upmigrate 100
write /proc/sys/kernel/sched_group_downmigrate 10
write /proc/sys/kernel/sched_walt_rotate_big_tasks 1

# cpusets
write /dev/cpuset/background/cpus 0-2
write /dev/cpuset/system-background/cpus 0-3
write /dev/cpuset/foreground/boost/cpus 4-7
write /dev/cpuset/foreground/cpus 0-2,4-7
write /dev/cpuset/top-app/cpus 0-7

write /proc/sys/kernel/sched_boost 0

# schedutil. No rate limits in either direction: frequency follows load
# immediately and, just as important for power, drops as soon as load goes.
# (The generic script meant to do this for all three policies but wrote
# policy0's down_rate_limit_us three times, leaving gold and gold+ at 20 ms.)
for p in 0 4 7; do
    P=/sys/devices/system/cpu/cpufreq/policy$p
    write $P/scaling_governor schedutil
    write $P/schedutil/up_rate_limit_us 0
    write $P/schedutil/down_rate_limit_us 0
    write $P/schedutil/pl 1
done
write /sys/devices/system/cpu/cpufreq/policy0/schedutil/hispeed_freq 1209600
write /sys/devices/system/cpu/cpufreq/policy4/schedutil/hispeed_freq 1612800
write /sys/devices/system/cpu/cpufreq/policy7/schedutil/hispeed_freq 1612800
write /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq 576000

# Bus DCVS
for device in /sys/devices/platform/soc; do
    for cpubw in $device/*cpu-cpu-llcc-bw/devfreq/*cpu-cpu-llcc-bw; do
        write $cpubw/governor bw_hwmon
        write $cpubw/bw_hwmon/mbps_zones "2288 4577 7110 9155 12298 14236 15258"
        write $cpubw/bw_hwmon/sample_ms 4
        write $cpubw/bw_hwmon/io_percent 50
        write $cpubw/bw_hwmon/hist_memory 20
        write $cpubw/bw_hwmon/hyst_length 10
        write $cpubw/bw_hwmon/down_thres 30
        write $cpubw/bw_hwmon/guard_band_mbps 0
        write $cpubw/bw_hwmon/up_scale 250
        write $cpubw/bw_hwmon/idle_mbps 1600
        write $cpubw/max_freq 14236
        write $cpubw/polling_interval 40
    done
    for llccbw in $device/*cpu-llcc-ddr-bw/devfreq/*cpu-llcc-ddr-bw; do
        write $llccbw/governor bw_hwmon
        write $llccbw/bw_hwmon/mbps_zones "1720 2929 3879 5931 6881 7980"
        write $llccbw/bw_hwmon/sample_ms 4
        write $llccbw/bw_hwmon/io_percent 80
        write $llccbw/bw_hwmon/hist_memory 20
        write $llccbw/bw_hwmon/hyst_length 10
        write $llccbw/bw_hwmon/down_thres 30
        write $llccbw/bw_hwmon/guard_band_mbps 0
        write $llccbw/bw_hwmon/up_scale 250
        write $llccbw/bw_hwmon/idle_mbps 1600
        write $llccbw/max_freq 6881
        write $llccbw/polling_interval 40
    done
    for npubw in $device/*npu-npu-ddr-bw/devfreq/*npu-npu-ddr-bw; do
        write /sys/devices/virtual/npu/msm_npu/pwr 1
        write $npubw/governor bw_hwmon
        write $npubw/bw_hwmon/mbps_zones "1720 2929 3879 5931 6881 7980"
        write $npubw/bw_hwmon/sample_ms 4
        write $npubw/bw_hwmon/io_percent 80
        write $npubw/bw_hwmon/hist_memory 20
        write $npubw/bw_hwmon/hyst_length 6
        write $npubw/bw_hwmon/down_thres 30
        write $npubw/bw_hwmon/guard_band_mbps 0
        write $npubw/bw_hwmon/up_scale 250
        write $npubw/bw_hwmon/idle_mbps 0
        write $npubw/polling_interval 40
        write /sys/devices/virtual/npu/msm_npu/pwr 0
    done
done
setprop vendor.dcvs.prop 1

# Allow deep sleep states.
write /sys/module/lpm_levels/parameters/sleep_disabled 0

configure_memory

# Let the kernel know our image version/variant/crm_version.
if [ -f /sys/devices/soc0/select_image ]; then
    echo 10 > /sys/devices/soc0/select_image
    echo "10:$(getprop ro.build.id):$(getprop ro.build.version.incremental)" > /sys/devices/soc0/image_version
    echo "$(getprop ro.product.name)-$(getprop ro.build.type)" > /sys/devices/soc0/image_variant
    echo "$(getprop ro.build.version.codename)" > /sys/devices/soc0/image_crm_version
fi

if [ "$(getprop persist.vendor.console.silent.config)" == "1" ]; then
    echo 0 > /proc/sys/kernel/printk
fi

misc_link=$(ls -l /dev/block/bootdevice/by-name/misc)
setprop persist.vendor.mmi.misc_dev_path ${misc_link##*>}

setprop vendor.post_boot.parsed 1
