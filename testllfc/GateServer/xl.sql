-- MySQL dump 10.13  Distrib 8.0.46, for Linux (x86_64)
--
-- Host: 127.0.0.1    Database: xl
-- ------------------------------------------------------
-- Server version	8.0.46-0ubuntu0.24.04.4

/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!50503 SET NAMES utf8mb4 */;
/*!40103 SET @OLD_TIME_ZONE=@@TIME_ZONE */;
/*!40103 SET TIME_ZONE='+00:00' */;
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;

--
-- Table structure for table `friend`
--

DROP TABLE IF EXISTS `friend`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `friend` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `self_id` int NOT NULL,
  `friend_id` int NOT NULL,
  `back` varchar(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci DEFAULT '',
  PRIMARY KEY (`id`) USING BTREE,
  UNIQUE KEY `self_friend` (`self_id`,`friend_id`) USING BTREE
) ENGINE=InnoDB AUTO_INCREMENT=123 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci ROW_FORMAT=DYNAMIC;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `friend`
--

LOCK TABLES `friend` WRITE;
/*!40000 ALTER TABLE `friend` DISABLE KEYS */;
INSERT INTO `friend` VALUES (55,1055,1054,'sqy'),(56,1054,1055,''),(61,1012,1056,'test28'),(62,1056,1012,''),(63,1012,1050,'test23'),(64,1050,1012,''),(81,1002,1019,'zack'),(82,1019,1002,''),(99,1064,1065,'ly'),(100,1065,1064,''),(103,1064,1066,'renji'),(104,1066,1064,''),(121,1064,1068,'menglei'),(122,1068,1064,'');
/*!40000 ALTER TABLE `friend` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `friend_apply`
--

DROP TABLE IF EXISTS `friend_apply`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `friend_apply` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `from_uid` int NOT NULL,
  `to_uid` int NOT NULL,
  `status` smallint NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`) USING BTREE,
  UNIQUE KEY `from_to_uid` (`from_uid`,`to_uid`) USING BTREE
) ENGINE=InnoDB AUTO_INCREMENT=88 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci ROW_FORMAT=DYNAMIC;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `friend_apply`
--

LOCK TABLES `friend_apply` WRITE;
/*!40000 ALTER TABLE `friend_apply` DISABLE KEYS */;
INSERT INTO `friend_apply` VALUES (6,1023,1002,0),(49,1054,1055,1),(52,1056,1012,0),(63,1019,1002,1),(64,1032,1035,0),(69,1063,1060,0),(70,1062,1012,0),(74,1065,1064,1),(87,1068,1064,1);
/*!40000 ALTER TABLE `friend_apply` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `user`
--

DROP TABLE IF EXISTS `user`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `user` (
  `id` int NOT NULL AUTO_INCREMENT,
  `uid` int NOT NULL DEFAULT '0',
  `name` varchar(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL DEFAULT '',
  `email` varchar(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL DEFAULT '',
  `pwd` varchar(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL DEFAULT '',
  `nick` varchar(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL DEFAULT '',
  `desc` varchar(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL DEFAULT '',
  `sex` int NOT NULL DEFAULT '0',
  `icon` varchar(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL DEFAULT '',
  PRIMARY KEY (`id`) USING BTREE,
  UNIQUE KEY `uid` (`uid`) USING BTREE,
  UNIQUE KEY `email` (`email`) USING BTREE,
  KEY `name` (`name`) USING BTREE
) ENGINE=InnoDB AUTO_INCREMENT=71 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci ROW_FORMAT=DYNAMIC;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `user`
--

LOCK TABLES `user` WRITE;
/*!40000 ALTER TABLE `user` DISABLE KEYS */;
INSERT INTO `user` VALUES (3,1002,'llfc','secondtonone1@163.com','654321)','llfc','',0,':/res/head_1.jpg'),(4,1003,'tc','18165031775@qq.com','123456','tc','',0,':/res/head_1.jpg'),(5,1004,'yuanweihua','1456188862@qq.com','}kyn;89>?<','yuanweihua','',0,':/res/head_1.jpg'),(6,1005,'test','2022202210033@whu.edu.cn','}kyn;89>?<','test','',0,':/res/head_1.jpg'),(8,1007,'fhr','3157199927@qq.com','xuexi1228','fhr','',0,':/res/head_1.jpg'),(9,1008,'zglx2008','zglx2008@163.com','123456','zglx2008','',0,':/res/head_1.jpg'),(13,1012,'resettest','1042958553@qq.com','230745','resettest','',0,':/res/head_1.jpg'),(14,1013,'rss_test','1685229455@qq.com','123456','rss_test','',0,':/res/head_1.jpg'),(15,1014,'123456789','1152907774@qq.com','123456789','123456789','',0,':/res/head_1.jpg'),(16,1015,'aaaaaaa','3031719794@qq.com','777777','aaaaaaa','',0,':/res/head_1.jpg'),(17,1016,'aaa','2996722319@qq.com','222222','aaa','',0,':/res/head_1.jpg'),(20,1019,'zack','1017234088@qq.com','654321)','zack','',0,':/res/head_1.jpg'),(21,1020,'aatext','1584736136@qq.com','745230','aatext','',0,':/res/head_1.jpg'),(22,1021,'ferrero1','1220292901@qq.com','1234','ferrero1','',0,':/res/head_1.jpg'),(23,1022,'ferrero2','15504616642@163.com','1234','ferrero2','',0,':/res/head_1.jpg'),(24,1023,'lyf','3194811890@qq.com','123456','lyf','',0,':/res/head_1.jpg'),(25,1024,'lh','2494350589@qq.com','fb8::>:;8<','lh','',0,':/res/head_1.jpg'),(26,1025,'jf','luojianfeng553@163.com','745230','jf','',0,':/res/head_1.jpg'),(33,1031,'zjm','1013049201@qq.com','745230','zjm','',0,':/res/head_1.jpg'),(34,1032,'yxc','1003314442@qq.com','123','yxc','',0,':/res/head_1.jpg'),(37,1035,'wangyu','962087148@qq.com','123456','wangyu','',0,':/res/head_1.jpg'),(39,1037,'chae','318192621@qq.com','123456','chae','',0,':/res/head_1.jpg'),(40,1038,'summer','1586856388@qq.com','654321)','summer','',0,':/res/head_2.jpg'),(44,1042,'zzz','3434321837@qq.com','|l~745','','',0,''),(45,1043,'sadda','z1668047679@163.com','123456','','',0,''),(46,1044,'qwe','1368326038@qq.com','1234','','',0,''),(52,1050,'test23','161945471@qq.com','230745','test23','',0,':/res/head_3.jpg'),(53,1051,'123','1767269204@qq.com','123','','',0,''),(54,1052,'zjc','766741776@qq.com','745230','','',0,''),(55,1053,'test_1','zzsr_0719@qq.com','123456','','',0,''),(56,1054,'sqy','3175614975@qq.com','745230','sqy','',0,':/res/head_2.jpg'),(57,1055,'ocz','q3175614975@163.com','745230','ocz','',0,':/res/head_3.jpg'),(58,1056,'test28','1669475972@qq.com','230745','test28','',0,':/res/head_1.jpg'),(60,1058,'NoOne','1764850358@qq.com','745230','','',0,''),(61,1059,'testx01','testx01@x.com','123456','','',0,''),(66,1064,'xl','745899278@qq.com','745230','xl','',0,':/res/head_xl.png'),(67,1065,'ly','15869988660@163.com','745230','ly','',0,':/res/head_ly.png'),(68,1066,'renji','xulei2006@qq.com','745230','renji','',0,':/res/head_4.jpg'),(69,1067,'linuxtester','linuxtest@test.com','123456','linuxtester','',0,':/res/head_1.jpg'),(70,1068,'menglei','3163797412@qq.com','745230','menglei','',0,':/res/head_2.jpg');
/*!40000 ALTER TABLE `user` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `user_id`
--

DROP TABLE IF EXISTS `user_id`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `user_id` (
  `id` int NOT NULL,
  PRIMARY KEY (`id`) USING BTREE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `user_id`
--

LOCK TABLES `user_id` WRITE;
/*!40000 ALTER TABLE `user_id` DISABLE KEYS */;
INSERT INTO `user_id` VALUES (1068);
/*!40000 ALTER TABLE `user_id` ENABLE KEYS */;
UNLOCK TABLES;
/*!40103 SET TIME_ZONE=@OLD_TIME_ZONE */;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;

-- Dump completed on 2026-11-03 20:26:26
