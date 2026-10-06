-- ============================================================
-- im-system MySQL 建表脚本（utf8mb4）
-- 执行: docker exec -i im-mysql mysql -uroot -pim123456 im_system < sql/im_system.sql
-- 数据库由 compose 自动创建（MYSQL_DATABASE=im_system）
-- ============================================================

-- 用户表
CREATE TABLE IF NOT EXISTS `user` (
  `id`             VARCHAR(64)  NOT NULL COMMENT 'uuid',
  `nickname`       VARCHAR(64)  NOT NULL COMMENT '昵称',
  `description`    VARCHAR(256) NOT NULL DEFAULT '' COMMENT '个人简介',
  `phone`          VARCHAR(20)  NOT NULL COMMENT '手机号即登录账号',
  `password_hash`  VARCHAR(128) NOT NULL DEFAULT '' COMMENT '密码哈希',
  `avatar_file_id` VARCHAR(64)  NOT NULL DEFAULT '' COMMENT '头像文件id',
  `create_time`    BIGINT      NOT NULL DEFAULT 0,
  `update_time`    BIGINT      NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_phone` (`phone`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户表';

-- 好友申请表
CREATE TABLE IF NOT EXISTS `friend_apply` (
  `id`          VARCHAR(64)  NOT NULL COMMENT 'uuid',
  `user_id`     VARCHAR(64)  NOT NULL COMMENT '申请人',
  `peer_id`     VARCHAR(64)  NOT NULL COMMENT '被申请人',
  `status`      TINYINT      NOT NULL DEFAULT 0 COMMENT '0待处理 1同意 2拒绝',
  `apply_note`  VARCHAR(256) NOT NULL DEFAULT '' COMMENT '验证消息',
  `create_time` BIGINT      NOT NULL DEFAULT 0,
  `update_time` BIGINT      NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `idx_peer_status` (`peer_id`, `status`),
  KEY `idx_user` (`user_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='好友申请表';

-- 好友关系表（互为好友写两行，方便单边查询）
CREATE TABLE IF NOT EXISTS `friend_relation` (
  `id`          VARCHAR(64) NOT NULL COMMENT 'uuid',
  `user_id`     VARCHAR(64) NOT NULL,
  `peer_id`     VARCHAR(64) NOT NULL,
  `create_time` BIGINT     NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_user_peer` (`user_id`, `peer_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='好友关系表';

-- 会话表
CREATE TABLE IF NOT EXISTS `chat_session` (
  `id`          VARCHAR(64) NOT NULL COMMENT 'uuid',
  `name`        VARCHAR(64) NOT NULL DEFAULT '' COMMENT '群名（单聊为空）',
  `type`        TINYINT     NOT NULL DEFAULT 0 COMMENT '0单聊 1群聊',
  `creator_id`  VARCHAR(64) NOT NULL DEFAULT '' COMMENT '群主（单聊为空）',
  `create_time` BIGINT     NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='会话表';

-- 会话成员表
CREATE TABLE IF NOT EXISTS `chat_session_member` (
  `id`          VARCHAR(64) NOT NULL COMMENT 'uuid',
  `session_id`  VARCHAR(64) NOT NULL,
  `user_id`     VARCHAR(64) NOT NULL,
  `create_time` BIGINT     NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_session_user` (`session_id`, `user_id`),
  KEY `idx_user` (`user_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='会话成员表';

-- 消息表（权威存储；ES 为搜索索引，见 sql/im-message.mapping.json）
CREATE TABLE IF NOT EXISTS `message` (
  `id`          VARCHAR(64)  NOT NULL COMMENT 'uuid',
  `session_id`  VARCHAR(64)  NOT NULL,
  `sender_id`   VARCHAR(64)  NOT NULL,
  `type`        TINYINT      NOT NULL DEFAULT 0 COMMENT '0文本 1图片 2文件 3语音',
  `content`     TEXT         NULL COMMENT '文本内容',
  `file_id`     VARCHAR(64)  NOT NULL DEFAULT '' COMMENT '图片/文件/语音文件id',
  `file_name`   VARCHAR(128) NOT NULL DEFAULT '',
  `file_size`   BIGINT       NOT NULL DEFAULT 0,
  `asr_text`    TEXT         NULL COMMENT '语音转写文本',
  `create_time` BIGINT      NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `idx_session_time` (`session_id`, `create_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='消息表';

-- 群聊事件表（第 8 张表，M5 新增）
-- 理由：原 7 表设计中"被拉入群"这类待处理事件无处存放；事件需记录
--       "谁邀请我进哪个群"，独立表最直接且可扩展（后续可加退群/踢人事件）。
CREATE TABLE IF NOT EXISTS `group_event` (
  `id`          VARCHAR(64) NOT NULL COMMENT 'uuid',
  `session_id`  VARCHAR(64) NOT NULL COMMENT '群会话 id',
  `user_id`     VARCHAR(64) NOT NULL COMMENT '被邀请人',
  `inviter_id`  VARCHAR(64) NOT NULL COMMENT '邀请人',
  `status`      TINYINT     NOT NULL DEFAULT 0 COMMENT '0未读 1已读',
  `create_time` BIGINT      NOT NULL DEFAULT 0 COMMENT '秒级时间戳',
  PRIMARY KEY (`id`),
  KEY `idx_user_status` (`user_id`, `status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='群聊事件表';

-- 文件元数据表（实体存本地磁盘）
CREATE TABLE IF NOT EXISTS `file` (
  `id`          VARCHAR(64)  NOT NULL COMMENT 'uuid',
  `file_name`   VARCHAR(128) NOT NULL,
  `file_size`   BIGINT       NOT NULL DEFAULT 0,
  `file_path`   VARCHAR(256) NOT NULL COMMENT '相对存储目录路径',
  `create_time` BIGINT      NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='文件元数据表';
