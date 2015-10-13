# lumia
lumia 作为galaxy机器管理平台，主要管理机器生命周期，目前实现流程:

```
                             Y
        死机汇报--> 死机检测 --> 机器重启
                       |               |
                       V               |
                重新部署galaxy agent<--
                        
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
