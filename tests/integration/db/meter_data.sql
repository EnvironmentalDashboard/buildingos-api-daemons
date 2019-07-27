CREATE TABLE `reading` (
  `id` bigint(20) NOT NULL,
  `meter_id` int(11) NOT NULL,
  `value` double NOT NULL,
  `recorded` int(11) NOT NULL,
  `resolution` smallint(6) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
