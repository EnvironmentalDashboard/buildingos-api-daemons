CREATE TABLE `meter` (
  `id` int(11) NOT NULL,
  `building_id` int(11) DEFAULT NULL,
  `buildingos_uuid` varchar(255) COLLATE utf8mb4_unicode_ci NOT NULL,
  `source` varchar(255) COLLATE utf8mb4_unicode_ci NOT NULL,
  `scope` varchar(255) COLLATE utf8mb4_unicode_ci NOT NULL,
  `resource` varchar(255) COLLATE utf8mb4_unicode_ci NOT NULL,
  `name` varchar(255) COLLATE utf8mb4_unicode_ci NOT NULL,
  `url` varchar(2000) COLLATE utf8mb4_unicode_ci NOT NULL,
  `current` double DEFAULT NULL,
  `units` varchar(255) COLLATE utf8mb4_unicode_ci NOT NULL,
  `calculated` tinyint(1) NOT NULL,
  `last_updated` int(11) NOT NULL,
  `gauges_using` smallint(6) NOT NULL,
  `charts_using` smallint(6) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;


INSERT INTO `meter` (`id`, `building_id`, `buildingos_uuid`, `source`, `scope`, `resource`, `name`, `url`, `current`, `units`, `calculated`, `last_updated`, `gauges_using`, `charts_using`) VALUES
(1, 1, '1', 'buildingos', 'Whole meter', 'Undefined', 'Test meter 1', 'https://environmentaldashboard.org/dummy-bos-data', 0, 'Other', 1, 0, 0, 1),
(2, 1, '2', 'buildingos', 'Whole meter', 'Undefined', 'Test meter 2', 'https://environmentaldashboard.org/dummy-bos-data', 0, 'Other', 1, 0, 0, 1),
(3, 1, '3', 'buildingos', 'Whole meter', 'Undefined', 'Test meter 3', 'https://environmentaldashboard.org/dummy-bos-data', 0, 'Other', 1, 0, 0, 1);
