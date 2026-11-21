-- ============================================================
-- 轻讯（QingXun）即时通讯项目 数据库初始化脚本（MySQL 8.x）
--
-- 用法（二选一）：
--   mysql -uroot -p < sql/schema.sql
--   或打开 Navicat / MySQL Workbench，执行本文件
--
-- 内容：
--   1. 创建 test 库和全部表（空表，不含示例数据）
--   2. 创建注册用的存储过程 reg_user
--   3. user_id 表插入初始值 1000，之后注册的用户从 1001 开始分配 uid
--
-- 注意：GateServer 注册接口依赖 reg_user 存储过程，
--       只建表不建存储过程的话，注册会失败。
-- ============================================================

CREATE DATABASE IF NOT EXISTS `test`
  DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE `test`;

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- ----------------------------
-- 用户与好友
-- ----------------------------
DROP TABLE IF EXISTS `user`;
CREATE TABLE `user` (
  `id` int NOT NULL AUTO_INCREMENT,
  `uid` int NOT NULL DEFAULT '0',
  `name` varchar(255) NOT NULL DEFAULT '',
  `email` varchar(255) NOT NULL DEFAULT '',
  `pwd` varchar(255) NOT NULL DEFAULT '',
  `nick` varchar(255) NOT NULL DEFAULT '',
  `desc` varchar(255) NOT NULL DEFAULT '',
  `sex` int NOT NULL DEFAULT '0',
  `icon` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`) USING BTREE,
  UNIQUE KEY `uid` (`uid`) USING BTREE,
  UNIQUE KEY `email` (`email`) USING BTREE,
  KEY `name` (`name`) USING BTREE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci ROW_FORMAT=DYNAMIC;

-- uid 分配器：reg_user 每次注册把 id + 1 作为新用户 uid
DROP TABLE IF EXISTS `user_id`;
CREATE TABLE `user_id` (
  `id` int NOT NULL,
  PRIMARY KEY (`id`) USING BTREE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC;

DROP TABLE IF EXISTS `friend`;
CREATE TABLE `friend` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `self_id` int NOT NULL,
  `friend_id` int NOT NULL,
  `back` varchar(255) DEFAULT '' COMMENT '好友备注',
  PRIMARY KEY (`id`) USING BTREE,
  UNIQUE KEY `self_friend` (`self_id`,`friend_id`) USING BTREE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci ROW_FORMAT=DYNAMIC;

-- 好友申请：status=0 待处理，1 已处理
DROP TABLE IF EXISTS `friend_apply`;
CREATE TABLE `friend_apply` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `from_uid` int NOT NULL,
  `to_uid` int NOT NULL,
  `status` smallint NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`) USING BTREE,
  UNIQUE KEY `from_to_uid` (`from_uid`,`to_uid`) USING BTREE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci ROW_FORMAT=DYNAMIC;

-- ----------------------------
-- 会话（私聊 / 群聊）
-- ----------------------------
DROP TABLE IF EXISTS `chat_thread`;
CREATE TABLE `chat_thread` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `type` enum('private','group') NOT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`) USING BTREE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC;

DROP TABLE IF EXISTS `private_chat`;
CREATE TABLE `private_chat` (
  `thread_id` bigint unsigned NOT NULL COMMENT '引用 chat_thread.id',
  `user1_id` bigint unsigned NOT NULL,
  `user2_id` bigint unsigned NOT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`thread_id`) USING BTREE,
  UNIQUE KEY `uniq_private_thread` (`user1_id`,`user2_id`) USING BTREE,
  KEY `idx_private_user1_thread` (`user1_id`,`thread_id`) USING BTREE,
  KEY `idx_private_user2_thread` (`user2_id`,`thread_id`) USING BTREE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC;

DROP TABLE IF EXISTS `group_chat`;
CREATE TABLE `group_chat` (
  `thread_id` bigint unsigned NOT NULL COMMENT '引用 chat_thread.id',
  `group_name` varchar(255) DEFAULT '',
  `owner_id` bigint unsigned NOT NULL DEFAULT '0',
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`thread_id`) USING BTREE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC;

DROP TABLE IF EXISTS `group_chat_member`;
CREATE TABLE `group_chat_member` (
  `thread_id` bigint unsigned NOT NULL COMMENT '引用 chat_thread.id',
  `user_id` bigint unsigned NOT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`thread_id`,`user_id`) USING BTREE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC;

-- ----------------------------
-- 消息
-- ----------------------------
DROP TABLE IF EXISTS `chat_message`;
CREATE TABLE `chat_message` (
  `message_id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `thread_id` bigint unsigned NOT NULL,
  `sender_id` bigint unsigned NOT NULL,
  `recv_id` bigint unsigned NOT NULL,
  `unique_id` varchar(64) DEFAULT '' COMMENT '客户端本地唯一 id，回执时用来匹配消息',
  `content` text NOT NULL COMMENT '文本内容或资源文件名',
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  `status` tinyint NOT NULL DEFAULT '0' COMMENT '0=未送达 1=已读 2=已送达(资源完成)',
  `msg_type` tinyint NOT NULL DEFAULT '0' COMMENT '0=文本 1=图片 3=文件',
  `file_size` bigint NOT NULL DEFAULT '0' COMMENT '图片/文件字节数',
  PRIMARY KEY (`message_id`) USING BTREE,
  KEY `idx_thread_created` (`thread_id`,`created_at`) USING BTREE,
  KEY `idx_thread_message` (`thread_id`,`message_id`) USING BTREE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC;

-- 单条消息删除记录：某用户删掉的 message_id（只影响自己，不删原消息）
DROP TABLE IF EXISTS `chat_message_deleted`;
CREATE TABLE `chat_message_deleted` (
  `message_id` bigint NOT NULL,
  `uid` int NOT NULL,
  PRIMARY KEY (`message_id`,`uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC;

-- 文件秒传索引：md5 → 服务器物理文件（Redis 只是缓存，这张表是最终真相）
DROP TABLE IF EXISTS `file_object`;
CREATE TABLE `file_object` (
  `md5` char(32) NOT NULL,
  `file_size` bigint NOT NULL DEFAULT '0',
  `file_path` varchar(512) NOT NULL COMMENT '相对 output/static 的路径，如 1002/xxx.jpg',
  `ref_count` int NOT NULL DEFAULT '1' COMMENT '被多少条消息引用（后续清理用）',
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`md5`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- 初始数据 + 注册存储过程
-- ----------------------------
-- 第一个注册用户 uid = 1001
INSERT IGNORE INTO `user_id` (`id`) VALUES (1000);

-- AI 助手固定用户：客户端直连百炼大模型，不可删除好友
INSERT IGNORE INTO `user` (`uid`, `name`, `email`, `pwd`, `nick`, `desc`, `sex`, `icon`)
VALUES (900000, 'AI助手', 'ai@localhost', '', 'AI助手', '百炼大模型助手', 0, ':/res/head_1.jpg');

DROP PROCEDURE IF EXISTS `reg_user`;
delimiter ;;
CREATE PROCEDURE `reg_user`(IN `new_name` VARCHAR(255),
    IN `new_email` VARCHAR(255),
    IN `new_pwd` VARCHAR(255),
    OUT `result` INT)
BEGIN
    -- 出错就回滚，并返回 -1
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET result = -1;
    END;

    START TRANSACTION;
    -- 用户名已存在返回 0
    IF EXISTS (SELECT 1 FROM `user` WHERE `name` = new_name) THEN
        SET result = 0;
        COMMIT;
    ELSE
        -- 邮箱已存在返回 0
        IF EXISTS (SELECT 1 FROM `user` WHERE `email` = new_email) THEN
            SET result = 0;
            COMMIT;
        ELSE
            -- 分配新 uid 并插入用户，返回新 uid
            UPDATE `user_id` SET `id` = `id` + 1;
            SELECT `id` INTO @new_id FROM `user_id`;
            INSERT INTO `user` (`uid`, `name`, `email`, `pwd`) VALUES (@new_id, new_name, new_email, new_pwd);
            SET result = @new_id;
            COMMIT;
        END IF;
    END IF;
END
;;
delimiter ;

SET FOREIGN_KEY_CHECKS = 1;
