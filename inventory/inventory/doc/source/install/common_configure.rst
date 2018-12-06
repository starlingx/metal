2. Edit the ``/etc/inventory/inventory.conf`` file and complete the following
   actions:

   * In the ``[database]`` section, configure database access:

     .. code-block:: ini

        [database]
        ...
        connection = mysql+pymysql://inventory:INVENTORY_DBPASS@controller/inventory
