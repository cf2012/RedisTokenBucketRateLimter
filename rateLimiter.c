#include "redismodule.h"
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

struct tokenbucket{
    size_t updatetime; // 更新时间
    size_t maxPermits; // 桶容量
    size_t curr_permits; // 当前令牌数
    size_t rate; // 令牌增长速度. 每秒产生多少令牌
};

/* Return the UNIX time in milliseconds */
mstime_t mstime(void) {
    struct timeval tv;
    long long msec;

    gettimeofday(&tv, NULL);
    msec = ((long long)tv.tv_sec)*1000; // 秒转毫秒
    msec = msec + tv.tv_usec/1000; // 微妙转毫秒

    return msec;
}


/** tokenbucket.set 桶名 容量 增长速度 [初始令牌数,默认为0] */
int SetBucket_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc)
{
    RedisModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 5 && argc != 4)
    {
        return RedisModule_WrongArity(ctx);
    }
    
    struct tokenbucket tb;
    memset(&tb, 0, sizeof(struct tokenbucket));

    // 桶更新时间
    tb.updatetime = mstime();

    // 容量
    long long ll =0L;
    if ((RedisModule_StringToLongLong(argv[2],&ll) != REDISMODULE_OK) ||
        (ll< 0)) {
        return RedisModule_ReplyWithError(ctx,"ERR invalid capacity");
    }
    tb.maxPermits = ll;

    // 令牌增长速度
    if ((RedisModule_StringToLongLong(argv[3],&ll) != REDISMODULE_OK) ||
        (ll< 0)) {
        return RedisModule_ReplyWithError(ctx,"ERR invalid rate");
    }
    tb.rate = ll;

    // 初始令牌数
    if (argc == 5) {
        if ((RedisModule_StringToLongLong(argv[4],&ll) != REDISMODULE_OK) ||
            (ll< 0) || ll > tb.maxPermits) {
            return RedisModule_ReplyWithError(ctx,"ERR invalid curr_permits");
        }
        tb.curr_permits = ll;
    } else {
        tb.curr_permits = 0; // 默认值
    }

    /** 写入 Redis */
    RedisModuleKey *key = RedisModule_OpenKey(ctx,argv[1],
        REDISMODULE_READ|REDISMODULE_WRITE);

    int key_type = RedisModule_KeyType(key);
    if (key_type != REDISMODULE_KEYTYPE_STRING && key_type != REDISMODULE_KEYTYPE_EMPTY) {
        RedisModule_CloseKey(key);
        return RedisModule_ReplyWithError(ctx,REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    RedisModuleString *value = RedisModule_CreateString(ctx, (const char *)&tb, sizeof(struct tokenbucket));

    if (REDISMODULE_ERR == RedisModule_StringSet(key, value)) {
        return RedisModule_ReplyWithError(ctx,"ERR Could not set string value");
    }

    RedisModule_CloseKey(key);
    RedisModule_ReplyWithSimpleString(ctx,"OK");

    return REDISMODULE_OK;
}

/** tokenbucket.info 桶名 */
int InfoBucket_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc)
{
    RedisModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 2)
    {
        return RedisModule_WrongArity(ctx);
    }

    /** 从 Redis 读取数据 */
    RedisModuleKey *key = RedisModule_OpenKey(ctx,argv[1], REDISMODULE_READ);

    int key_type = RedisModule_KeyType(key);
    if (key_type != REDISMODULE_KEYTYPE_STRING) {
        RedisModule_CloseKey(key);
        return RedisModule_ReplyWithError(ctx,REDISMODULE_ERRORMSG_WRONGTYPE);
    }
    size_t len;
    char *s = RedisModule_StringDMA(key,&len,REDISMODULE_READ);
    if (len != sizeof(struct tokenbucket)) {
        RedisModule_CloseKey(key);
        return RedisModule_ReplyWithError(ctx,REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    struct tokenbucket tb;
    memset(&tb, 0, sizeof(struct tokenbucket));
    memcpy(&tb, s, len);

    RedisModule_CloseKey(key);

    RedisModule_ReplyWithArray(ctx,4);
    char tmpbuf[64]="\0";

    #define MY_REPLY_WITH_SIMPLE_STRING(x, y) do{ \
        snprintf(tmpbuf, sizeof(tmpbuf), #x": %zu", y); \
        RedisModule_ReplyWithSimpleString(ctx,tmpbuf); \
    }while(0)

    MY_REPLY_WITH_SIMPLE_STRING("updatetime", tb.updatetime); // 更新时间
    MY_REPLY_WITH_SIMPLE_STRING("maxPermits", tb.maxPermits); // 桶容量
    MY_REPLY_WITH_SIMPLE_STRING("curr_permits", tb.curr_permits); // 当前令牌数
    MY_REPLY_WITH_SIMPLE_STRING("rate", tb.rate); // 令牌增长速度. 每秒产生多少令牌

    return REDISMODULE_OK;
}

/** tokenbucket.get 桶名 令牌数  */
int GetBucket_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc)
{
    RedisModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 3) {
        return RedisModule_WrongArity(ctx);
    }

    /** 从 Redis 读取数据 */
    RedisModuleKey *key = RedisModule_OpenKey(ctx,argv[1], REDISMODULE_READ|REDISMODULE_WRITE);

    int key_type = RedisModule_KeyType(key);
    if (key_type != REDISMODULE_KEYTYPE_STRING) {
        RedisModule_CloseKey(key);
        return RedisModule_ReplyWithError(ctx,REDISMODULE_ERRORMSG_WRONGTYPE);
    }
    size_t len;
    char *s = RedisModule_StringDMA(key,&len,REDISMODULE_WRITE);
    if (len != sizeof(struct tokenbucket)) {
        RedisModule_CloseKey(key);
        return RedisModule_ReplyWithError(ctx,REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    struct tokenbucket tb;
    memset(&tb, 0, sizeof(struct tokenbucket));
    memcpy(&tb, s, len);

    // 计算令牌数量
    mstime_t now = mstime();
    size_t expect_permits = 0L;
    if (now >= tb.updatetime) {
        expect_permits = (now - tb.updatetime)/1000 * tb.rate + tb.curr_permits;
        expect_permits = expect_permits > tb.maxPermits ? tb.maxPermits : expect_permits;
    } else {
        return RedisModule_ReplyWithError(ctx, "Error, Wrong time");
    }

    // 用户要拿的令牌数量
    long long ll =0L;
    if ((RedisModule_StringToLongLong(argv[2],&ll) != REDISMODULE_OK) ||
        (ll< 0)) {
        return RedisModule_ReplyWithError(ctx,"ERR invalid num of permits");
    }

    if ( expect_permits >= ll ){
        RedisModule_ReplyWithLongLong(ctx, ll);
        tb.curr_permits = expect_permits - ll;
    } else {
        RedisModule_ReplyWithLongLong(ctx, 0);
        tb.curr_permits = expect_permits;
    }
    tb.updatetime = now;
    memcpy(s, &tb, sizeof(struct tokenbucket));
    RedisModule_CloseKey(key);

    return REDISMODULE_OK;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc)
{
    // 初始化. 注册模块名称
    if (RedisModule_Init(ctx, "tokenbucket", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR)
    {
        return REDISMODULE_ERR;
    }

#define REDIS_MODULE_CREATE_COMMAND(c, f, m)  do{ \
    if (RedisModule_CreateCommand(ctx, c, \
                                  f, m, 0, 0, 0) == REDISMODULE_ERR) { \
        return REDISMODULE_ERR; \
    } \
}while(0)

    // 注册命令
    REDIS_MODULE_CREATE_COMMAND("tokenbucket.set", SetBucket_RedisCommand, "write");
    REDIS_MODULE_CREATE_COMMAND("tokenbucket.info", InfoBucket_RedisCommand, "readonly");
    REDIS_MODULE_CREATE_COMMAND("tokenbucket.get", GetBucket_RedisCommand, "write");

    return REDISMODULE_OK;
}
