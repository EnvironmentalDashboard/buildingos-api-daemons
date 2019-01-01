CREATE TABLE `meters` (
  `id` int(11) NOT NULL,
  `org_id` int(11) NOT NULL DEFAULT '0',
  `bos_uuid` varchar(255) DEFAULT NULL,
  `building_id` int(11) NOT NULL,
  `source` enum('buildingos','user','glos','airnow') NOT NULL,
  `scope` varchar(255) NOT NULL DEFAULT '',
  `resource` varchar(255) NOT NULL DEFAULT '',
  `name` varchar(255) NOT NULL,
  `url` varchar(2000) DEFAULT NULL,
  `building_url` varchar(2000) DEFAULT NULL,
  `current` decimal(12,3) DEFAULT NULL,
  `units` varchar(255) NOT NULL,
  `calculated` tinyint(1) NOT NULL DEFAULT '1',
  `live_last_updated` int(10) NOT NULL DEFAULT '0',
  `quarterhour_last_updated` int(10) NOT NULL DEFAULT '0',
  `hour_last_updated` int(10) NOT NULL DEFAULT '0',
  `month_last_updated` int(10) NOT NULL DEFAULT '0',
  `gauges_using` tinyint(1) NOT NULL DEFAULT '0' COMMENT '# of saved gauges using this meter',
  `timeseries_using` tinyint(1) NOT NULL DEFAULT '0' COMMENT '# of saved time series using this meter',
  `for_orb` tinyint(1) NOT NULL DEFAULT '0' COMMENT '# of Oberlin''s old orbs using this meter',
  `orb_server` tinyint(1) NOT NULL DEFAULT '0' COMMENT 'for Jeremy''s app'
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

INSERT INTO `meters` (`id`, `org_id`, `bos_uuid`, `building_id`, `source`, `scope`, `resource`, `name`, `url`, `building_url`, `current`, `units`, `calculated`, `live_last_updated`, `quarterhour_last_updated`, `hour_last_updated`, `month_last_updated`, `gauges_using`, `timeseries_using`, `for_orb`, `orb_server`) VALUES
(1, 0, NULL, 1, 'buildingos', 'Other', 'Undefined', 'Test meter 1', 'https://environmentaldashboard.org/dummy-bos-data', '', '0.000', 'Kilowatts', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(2, 0, NULL, 1, 'buildingos', 'Other', 'Undefined', 'Test meter 2', 'https://environmentaldashboard.org/dummy-bos-data', '', '0.000', 'Kilowatts', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(3, 0, NULL, 1, 'buildingos', 'Other', 'Undefined', 'Test meter 3', 'https://environmentaldashboard.org/dummy-bos-data', '', '0.000', 'Kilowatts', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(4, 0, NULL, 1, 'buildingos', 'Other', 'Undefined', 'Test meter 4', 'https://environmentaldashboard.org/dummy-bos-data', '', '0.000', 'Kilowatts', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(5, 0, NULL, 1, 'buildingos', 'Other', 'Undefined', 'Test meter 5', 'https://environmentaldashboard.org/dummy-bos-data', '', '0.000', 'Kilowatts', 1, 0, 0, 0, 0, 0, 0, 0, 0);


ALTER TABLE `meters`
  ADD PRIMARY KEY (`id`),
  -- ADD UNIQUE KEY `url` (`url`), -- disable for testing bc we're gonna use the same url for all test meters
  ADD UNIQUE KEY `bos_uuid` (`bos_uuid`);

ALTER TABLE `meters`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;
