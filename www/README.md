This requires a MySQL or MariaDB database to work.

How to do this:
- Make sure your server is running a MySQL/Mariadb database you have access to.
- Create user 'epd' in the database. Assign a password.
- Copy config.php.example to config.php. Edit the $pass variable to be the actual password.
- Create database 'epd', grant user 'epd' all permissions
- Create the structure:
  mysql -u epd -p epd < create-database.sql

This also needs a image -> EPD binary program that is written in C to work. This
needs to be compiled. To do so:
- Make sure gcc and make are installed on the host
- Install libgd-dev (the development package for libgd)
- cd conv; make
- Make sure php is allowed to run unix executables

