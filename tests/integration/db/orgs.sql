CREATE TABLE `orgs` (
  `id` int(11) NOT NULL,
  `api_id` int(11) NOT NULL DEFAULT '0',
  `name` varchar(255) NOT NULL DEFAULT '',
  `url` varchar(2000) NOT NULL DEFAULT ''
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

INSERT INTO `orgs` (`id`, `api_id`, `name`, `url`) VALUES
(1, 1, 'Oberlin College', 'https://api.buildingos.com/organizations/112');

ALTER TABLE `orgs`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `url` (`url`);

