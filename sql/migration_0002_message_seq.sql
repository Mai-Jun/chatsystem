-- M10 迁移 0002：message 表增加 seq 排序键
-- 背景：create_time 仅秒级精度，同一秒内消息顺序不稳定（压测/刷屏可见乱序）。
-- 说明：seq 为存储层内部排序键，不进 proto/ODB 实体；新插入由 MySQL 自增分配，
--       历史查询按 (create_time DESC, seq DESC) 保证稳定顺序。
ALTER TABLE `message`
  ADD COLUMN `seq` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  ADD UNIQUE KEY `uk_seq` (`seq`);
