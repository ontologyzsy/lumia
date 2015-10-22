# lumia
lumia 作为galaxy机器管理平台，主要包含功能
* sata/ssd故障检测
* 机器生命周期管理
目前实现流程:

```
                             Y
        死机汇报--> 死机检测 --> 机器重启 <-- 硬件故障 <-- agent实时故障检测
                       |               |
                       V               |
                重新部署galaxy agent<--Y---> 重装系统--
                                  ^                    |
                                  |---------------------
                        
```

## build4baiduer
```
comake2 -UB && comake2 && make -j8 -s
```

## 本地运行
```
cd sandbox && sh quick_test.sh
```

## 查看机器状态

```
./lumia show -i <ip>
```

## 汇报死机

```
./lumia report -i <ip>
```
