CREATE TABLE `api` (
  `id` int(11) NOT NULL,
  `user_id` int(11) NOT NULL DEFAULT '0',
  `client_id` varchar(255) NOT NULL,
  `client_secret` varchar(255) NOT NULL,
  `username` varchar(255) NOT NULL,
  `password` varchar(255) NOT NULL,
  `token` varchar(255) NOT NULL DEFAULT '',
  `token_updated` int(11) NOT NULL DEFAULT '0'
) ENGINE=InnoDB DEFAULT CHARSET=latin1;


INSERT INTO `api` (`id`, `user_id`, `client_id`, `client_secret`, `username`, `password`, `token`, `token_updated`) VALUES
(1, 1, 'OMITTED', 'OMITTED', 'OMITTED', 'OMITTED', 'OMITTED', 0);


ALTER TABLE `api`
  ADD PRIMARY KEY (`id`);

ALTER TABLE `api`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;
