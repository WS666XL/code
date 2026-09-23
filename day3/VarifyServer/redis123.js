
const config_module = require('./config')
const Redis = require("ioredis");

// 创建Redis客户端实例
const RedisCli = new Redis({
  host: config_module.redis_host,       // Redis服务器主机名
  port: config_module.redis_port,        // Redis服务器端口号
  password: config_module.redis_passwd, // Redis密码
});


/**
 * 监听错误信息
 *
 * 【重要】这里绝对不能调用 RedisCli.quit()。
 * quit() 是"优雅关闭"语义：它会把连接彻底关掉，并**禁止 ioredis 自动重连**。
 * 一旦 Redis 重启 / 网络抖动触发了 error 事件，quit() 就等于让客户端"自杀"，
 * 之后所有命令都会抛 Connection is closed. —— 即使 Redis 已经恢复也救不回来。
 *
 * ioredis 自带重连机制（约 50ms 起步、指数退避、无限重试），
 * 断线后只要 Redis 回来它自己就会接上，我们只需要打日志。
 */
RedisCli.on("error", function (err) {
  console.log("RedisCli connect error", err);
});

// 连接状态日志：排查"到底连没连上"时一眼就能看出来
RedisCli.on("connect", function () {
  console.log("Redis connected");
});

RedisCli.on("reconnecting", function (delay) {
  console.log("Redis reconnecting in " + delay + " ms ...");
});

/**
 * 根据key获取value
 * @param {*} key 
 * @returns 
 */
async function GetRedis(key) {

    try{
        const result = await RedisCli.get(key)
        if(result === null){
          console.log('result:','<'+result+'>', 'This key cannot be find...')
          return null
        }
        console.log('Result:','<'+result+'>','Get key success!...');
        return result
    }catch(error){
        console.log('GetRedis error is', error);
        return null
    }

  }

/**
 * 根据key查询redis中是否存在key
 * @param {*} key 
 * @returns 
 */
async function QueryRedis(key) {
    try{
        const result = await RedisCli.exists(key)
        //  判断该值是否为空 如果为空返回null
        if (result === 0) {
          console.log('result:<','<'+result+'>','This key is null...');
          return null
        }
        console.log('Result:','<'+result+'>','With this value!...');
        return result
    }catch(error){
        console.log('QueryRedis error is', error);
        return null
    }

  }

/**
 * 设置key和value，并过期时间
 * @param {*} key 
 * @param {*} value 
 * @param {*} exptime 
 * @returns 
 */
async function SetRedisExpire(key,value, exptime){
    try{
        // 设置键和值
        await RedisCli.set(key,value)
        // 设置过期时间（以秒为单位）
        await RedisCli.expire(key, exptime);
        return true;
    }catch(error){
        console.log('SetRedisExpire error is', error);
        return false;
    }
}

/**
 * 退出函数
 */
function Quit(){
    RedisCli.quit();
}

module.exports = {GetRedis, QueryRedis, Quit, SetRedisExpire,}