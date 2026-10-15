/*
 Navicat Premium Dump SQL

 Source Server         : llfcmysql
 Source Server Type    : MySQL
 Source Server Version : 90400 (9.4.0)
 Source Host           : localhost:3308
 Source Schema         : xulei

 Target Server Type    : MySQL
 Target Server Version : 90400 (9.4.0)
 File Encoding         : 65001

 Date: 15/10/2026 19:24:21
*/

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- ----------------------------
-- Table structure for friend_apply
-- ----------------------------
DROP TABLE IF EXISTS `friend_apply`;
CREATE TABLE `friend_apply`  (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `from_uid` int NOT NULL,
  `to_uid` int NOT NULL,
  `status` smallint NOT NULL,
  PRIMARY KEY (`id`) USING BTREE,
  UNIQUE INDEX `from_to_uid`(`from_uid` ASC, `to_uid` ASC) USING BTREE
) ENGINE = InnoDB CHARACTER SET = utf8mb4 COLLATE = utf8mb4_0900_ai_ci ROW_FORMAT = Dynamic;

-- ----------------------------
-- Records of friend_apply
-- ----------------------------

-- ----------------------------
-- Table structure for user
-- ----------------------------
DROP TABLE IF EXISTS `user`;
CREATE TABLE `user`  (
  `id` int NOT NULL AUTO_INCREMENT,
  `uid` int NOT NULL,
  `name` varchar(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  `email` varchar(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  `pwd` varchar(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
  PRIMARY KEY (`id`) USING BTREE
) ENGINE = InnoDB AUTO_INCREMENT = 3 CHARACTER SET = utf8mb4 COLLATE = utf8mb4_0900_ai_ci ROW_FORMAT = Dynamic;

-- ----------------------------
-- Records of user
-- ----------------------------
INSERT INTO `user` VALUES (1, 2, 'xl', '745899278@qq.com', '745230');
INSERT INTO `user` VALUES (2, 3, 'ly', '15869988660@163.com', '745230');

-- ----------------------------
-- Table structure for user_id
-- ----------------------------
DROP TABLE IF EXISTS `user_id`;
CREATE TABLE `user_id`  (
  `id` int NOT NULL AUTO_INCREMENT,
  PRIMARY KEY (`id`) USING BTREE
) ENGINE = InnoDB AUTO_INCREMENT = 4 CHARACTER SET = utf8mb4 COLLATE = utf8mb4_0900_ai_ci ROW_FORMAT = Dynamic;

-- ----------------------------
-- Records of user_id
-- ----------------------------
INSERT INTO `user_id` VALUES (3);

-- ----------------------------
-- Procedure structure for reg_user
-- ----------------------------
DROP PROCEDURE IF EXISTS `reg_user`;
delimiter ;;
CREATE PROCEDURE `reg_user`(IN `new_name` VARCHAR(255), 
    IN `new_email` VARCHAR(255), 
    IN `new_pwd` VARCHAR(255), 
    OUT `result` INT)
BEGIN
    -- 如果在执行过程中遇到任何错误，则回滚事务
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        -- 回滚事务
        ROLLBACK;
        -- 设置返回值为-1，表示错误
        SET result = -1;
    END;

    -- 开始事务
    START TRANSACTION;

    -- 检查用户名是否已存在
    IF EXISTS (SELECT 1 FROM `user` WHERE `name` = new_name) THEN
        SET result = 0; -- 用户名已存在
        COMMIT;
    ELSE
        -- 用户名不存在，检查email是否已存在
        IF EXISTS (SELECT 1 FROM `user` WHERE `email` = new_email) THEN
            SET result = 0; -- email已存在
            COMMIT;
        ELSE
            -- email也不存在，更新user_id表
            UPDATE `user_id` SET `id` = `id` + 1;

            -- 获取更新后的id
            SELECT `id` INTO @new_id FROM `user_id`;

            -- 在user表中插入新记录
            INSERT INTO `user` (`uid`, `name`, `email`, `pwd`) VALUES (@new_id, new_name, new_email, new_pwd);
            -- 设置result为新插入的uid
            SET result = @new_id; -- 插入成功，返回新的uid
            COMMIT;

        END IF;
    END IF;

END
;;
delimiter ;

SET FOREIGN_KEY_CHECKS = 1;
