CREATE TABLE `daemon` (
  `id` int(11) NOT NULL,
  `updating_id` int(11) DEFAULT NULL,
  `host` varchar(20) COLLATE utf8mb4_unicode_ci NOT NULL,
  `enabled` tinyint(1) NOT NULL,
  `last_updated` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;


ALTER TABLE `daemons`
  ADD PRIMARY KEY (`host`);

