# RedisTokenBucketRateLimter

A rolling rate limit Redis module

使用 RedisModule 实现了令牌桶的限流算法

## 使用

模块提供了3个命令:

1. `tokenbucket.set 桶名 容量 每秒新增令牌数量 \[初始令牌数量,默认为0\]`. 成功返回 `OK`
2. `tokenbucket.info 桶名`  用于查询桶信息
3. `tokenbucket.get 桶名 要拿的令牌数`  获取令牌. 成功,返回令牌数. 失败,返回0

在`redis-cli`中:

    127.0.0.1:6379> tokenbucket.set b1 200 10
    OK
    127.0.0.1:6379> tokenbucket.info b1
    1) updatetime: 1575907190571
    2) maxPermits: 200
    3) curr_permits: 0
    4) rate: 10
    127.0.0.1:6379> tokenbucket.get b1 30
    (integer) 30
    127.0.0.1:6379> tokenbucket.get b1 30
    (integer) 30
