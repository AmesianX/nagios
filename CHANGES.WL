List of changes in this version of Nagios that are not in Nagios Core 4:

1) SAVED_DATA (applied to hosts and services)
  Plugins may provide additional data after || that is saved by nagios and avaialable
  using specialized MACROs for future calls to the plugin

2) cgi_debugfile= directive for cgi.cfg which can specify file where debug data would go into
  Debugging is provided by very simple logit() function specific to cgis, no file trunation support
  (in nagios 4 this function is just empty for cgis)

3) Like Nagios 3 the code allows for any configs in main nagios.cfg


Code related changes that would not affect configuration (this maybe incomplete);

- cleanup of error path and error handling in objects.c
- all instances of not-thread safe strtok() in core code
  (but not cgis or nagiostats) are replaced with my_strsep() and my_str2parts()
