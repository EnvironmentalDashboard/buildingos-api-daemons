CREATE TABLE `daemons` (
  `host` varchar(20) NOT NULL DEFAULT '',
  `enabled` tinyint(1) NOT NULL DEFAULT '0',
  `updating_meter` int(11) NOT NULL DEFAULT '0' COMMENT 'The ID of the meter this daemon is currently updating',
  `last_updated` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=latin1;


ALTER TABLE `daemons`
  ADD PRIMARY KEY (`host`);

