CREATE TABLE `buildingos_api` (
  `id` int(11) NOT NULL,
  `community_id` int(11) NOT NULL,
  `client_id` varchar(255) COLLATE utf8mb4_unicode_ci NOT NULL,
  `client_secret` varchar(255) COLLATE utf8mb4_unicode_ci NOT NULL,
  `username` varchar(255) COLLATE utf8mb4_unicode_ci NOT NULL,
  `password` varchar(255) COLLATE utf8mb4_unicode_ci NOT NULL,
  `token` varchar(255) COLLATE utf8mb4_unicode_ci NOT NULL,
  `token_updated` datetime NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT INTO `buildingos_api` (`id`, `community_id`, `client_id`, `client_secret`, `username`, `password`, `token`, `token_updated`) VALUES
(1, 1, 'client_id', 'OMITTED', 'OMITTED', 'OMITTED', 'OMITTED', '2000-01-01 00:00:00');
