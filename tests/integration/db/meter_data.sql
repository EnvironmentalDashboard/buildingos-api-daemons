CREATE TABLE `meter_data` (
  `id` int(11) UNSIGNED NOT NULL,
  `meter_id` int(11) NOT NULL DEFAULT '0',
  `value` decimal(12,3) DEFAULT NULL,
  `recorded` int(10) NOT NULL DEFAULT '0',
  `resolution` enum('live','quarterhour','hour','day','month','other') NOT NULL DEFAULT 'live'
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

ALTER TABLE `meter_data`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `meter_id` (`meter_id`,`recorded`,`resolution`);

ALTER TABLE `meter_data`
  MODIFY `id` int(11) UNSIGNED NOT NULL AUTO_INCREMENT;
