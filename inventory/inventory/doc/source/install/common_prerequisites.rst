Prerequisites
-------------

Before you install and configure the inventory service,
you must create a database, service credentials, and API endpoints.

#. To create the database, complete these steps:

   * Use the database access client to connect to the database
     server as the ``root`` user:

     .. code-block:: console

        $ mysql -u root -p

   * Create the ``inventory`` database:

     .. code-block:: none

        CREATE DATABASE inventory;

   * Grant proper access to the ``inventory`` database:

     .. code-block:: none

        GRANT ALL PRIVILEGES ON inventory.* TO 'inventory'@'localhost' \
          IDENTIFIED BY 'INVENTORY_DBPASS';
        GRANT ALL PRIVILEGES ON inventory.* TO 'inventory'@'%' \
          IDENTIFIED BY 'INVENTORY_DBPASS';

     Replace ``INVENTORY_DBPASS`` with a suitable password.

   * Exit the database access client.

     .. code-block:: none

        exit;

#. Source the ``admin`` credentials to gain access to
   admin-only CLI commands:

   .. code-block:: console

      $ . admin-openrc

#. To create the service credentials, complete these steps:

   * Create the ``inventory`` user:

     .. code-block:: console

        $ openstack user create --domain default --password-prompt inventory

   * Add the ``admin`` role to the ``inventory`` user:

     .. code-block:: console

        $ openstack role add --project service --user inventory admin

   * Create the inventory service entities:

     .. code-block:: console

        $ openstack service create --name inventory --description "inventory" inventory

#. Create the inventory service API endpoints:

   .. code-block:: console

      $ openstack endpoint create --region RegionOne \
        inventory public http://controller:XXXX/vY/%\(tenant_id\)s
      $ openstack endpoint create --region RegionOne \
        inventory internal http://controller:XXXX/vY/%\(tenant_id\)s
      $ openstack endpoint create --region RegionOne \
        inventory admin http://controller:XXXX/vY/%\(tenant_id\)s
