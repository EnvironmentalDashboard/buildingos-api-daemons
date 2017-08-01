<?php
#!/usr/local/bin/php
error_reporting(-1);
set_time_limit(0);
ini_set('display_errors', 'On');
date_default_timezone_set('America/New_York');
chdir(__DIR__);
require '../includes/db.php'; // Has $db
$pop = 3;
foreach ($db->query('SELECT pid FROM daemons WHERE enabled = 1') as $daemon) {
  if (!file_exists("/proc/{$daemon['pid']}")) { // process is not running, but is in db
    $db->query("DELETE FROM daemons WHERE pid = {$daemon['pid']}");
  } else {
    $pop--;
  }
}
foreach ($db->query('SELECT pid FROM daemons WHERE enabled = 0') as $daemon) {
  if (file_exists("/proc/{$daemon['pid']}")) {
    shell_exec("kill {$daemon['pid']}");
  }
  $db->query("DELETE FROM daemons WHERE pid = {$daemon['pid']}");
}
for ($i=0; $i < $pop; $i++) { 
  `/var/www/html/oberlin/daemons/buildingosd -d`;
}
?>
