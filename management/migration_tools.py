"""
The purpose of this file is to provide a set of tools for migrating collected data from one server 
to another, even while devices are online and collecting.


When configuring a new machine, the first thing to do is to copy everything from the
old server to the new server: 

On source machine: 
sudo vi /etc/ssh/sshd_config
>>> PermitRootLogin yes
sudo service ssh restart

On dest machine, pull files from old machine:

rsync -aHxv --numeric-ids --exclude=/etc/fstab --exclude=/etc/network/* --exclude=/proc/*
    --exclude=/tmp/* --exclude=/sys/* --exclude=/dev/* --exclude=/mnt/* --exclude=/boot/*
    --exclude=/var/db/backups/* root@198.46.139.101:/* /

Restart dest machine, make sure can login as root and chris; also make sure you can log in to the
older machine from the newer one withtout entering a password (see
https://www.tecmint.com/ssh-passwordless-login-using-ssh-keygen-in-5-easy-steps/).

At this point, all devices will still be writing to the old machine; delete the telemetry records in
our new database:

sudo su -c "psql thingsboard -c \"delete from ts_kv; delete from ts_kv_latest;\"" postgres
sudo su -c "psql thingsboard -c \"select count(*) from ts_kv;\"" postgres

Verify access to thingsboard on new server using IP address.    

Now change the sensorbot.org DNS settings to aim at the new machine.  As devices start sending
telemetry to the new server, their old data can be tansferred.  If necessary, use a minor software
update to force devices to restart and make fresh DNS requests.  Migrating a few devices at a time
is fine; it is best to make a list of device you think need to be migrated and manually track your
progress.

Use the find_unmigrated_devices() function to help deterine which devices might have data that
needs to be moved.  This also assesses whether devices are writing to the new server yet.

print_item_counts() shows how many records for a device exists on the origin and destination servers.

"""

# from docopt import docopt
import birdhouse_utils
import paramiko    # pip install paramiko
import requests
import time
import datetime
from thingsboard_api_tools import TbApi     # pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
import logging
from config import thingsboard_username, thingsboard_password, ssh_username, ssh_password

log = logging.getLogger("migrate")
logging.basicConfig(level=logging.INFO)

logging.getLogger("paramiko").setLevel(logging.WARNING)     # Quiet this dude down a notch


old_server_ip = "162.212.157.80"
new_server_ip = "192.210.218.130"  

devices_to_migrate = [8, 14, 16, 19, 22, 23, 48]   # Note that this list  will be sorted before processing!

# Things that probably will never change:
SSH_PORT = 22
POSTGRES_COMMAND = "sudo -u postgres psql -qAtX -d thingsboard -c"
COLUMN_LIST = "entity_type, entity_id, key, ts, bool_v, str_v, long_v, dbl_v"

MINUTES = 60
HOURS = 60 * MINUTES
DAYS = 24 * HOURS
WEEKS = 7 * DAYS


def main():
    find_unmigrated_devices(old_server_ip, new_server_ip)   # This still isn't right -- devices that are not online are marked as not ready for migration... maybe other problems
    exit()

    old_client = create_client(old_server_ip)
    new_client = create_client(new_server_ip)

    print_item_counts(old_client, new_client, devices_to_migrate)
    exit()


    # Make sure everything is clear sailing
    check_for_remigration(devices_to_migrate)   # Make sure we're not reprocessing an device we've already migrated
    check_for_dupes(devices_to_migrate)         # Make sure we haven't entered the same number twice in our processing list
    ensure_devices_exist(devices_to_migrate)    # Make sure device is defined on both old and new servers
    
    verify_devices_remapped(devices_to_migrate, min_interval=60 * MINUTES)  # Ensure device has started sending telemetry to new server -- don't migrate until it has

    print("Passed preflight checks... ready to migrate")
    exit()

    print()
    print("=== Beginning Migration ===")

    print(f"Processing devices {devices_to_migrate}...")

    first = True

    for bhnum in devices_to_migrate:
        formatted_num = birdhouse_utils.make_device_number(bhnum)
        
        if not first:
            print("==========")

        print(f"Starting load for {birdhouse_utils.make_device_name(bhnum)}...")

        filename = f"/tmp/ts_kv_{formatted_num}.csv"        # Filename on remote servers

        export_data(old_client, bhnum, filename)
        md5 = get_md5(old_client, filename)
        if md5.strip() == "":
            print(f"Could not find database export file {filename} on old server... Aborting.")
            exit()
        transfer_file(new_client, old_server_ip, filename, md5)
        load_data(new_client, filename, formatted_num)

        delete_file(old_client, filename, "client")
        delete_file(new_client, filename, "server")

        print(f"Data loaded for device {formatted_num}")
        first = False

    old_client.close()
    new_client.close()


def find_unmigrated_devices(old_ip, new_ip):
    tbapi_old = TbApi(birdhouse_utils.make_mothership_url(old_ip), thingsboard_username, thingsboard_password)
    tbapi_new = TbApi(birdhouse_utils.make_mothership_url(new_ip), thingsboard_username, thingsboard_password)

    old_client = create_client(old_ip)
    new_client = create_client(new_ip)

    devices_on_old = tbapi_old.get_all_devices()
    devices_on_new = tbapi_new.get_all_devices()

    unmigrated_ready_to_go, migrated, unmigrated, no_data, no_data_2, new_only = list(), list(), list(), list(), list(), list()

    for dev in devices_on_old:
        name = dev["name"]

        print(f"Device {name}")
        has_telem_on_old = has_telemetry(tbapi_old, dev)
        has_telem_on_new = has_telemetry(tbapi_new, dev)

        if not device_has_data(tbapi_old, old_client, name):
            no_data_2.append(name)

        if has_telem_on_old:
            if has_telem_on_new:
                # Telemetry exists on both servers; has it been migrated, or merely switched itself over?
                if device_has_been_migrated(tbapi_old, old_client, tbapi_new, new_client, name):
                    migrated.append(name)
                else:
                    unmigrated_ready_to_go.append(name)
            else:  # on old, not on new
                unmigrated.append(name)
        else:  # not old
            if has_telem_on_new:
                new_only.append(name)
            else:
                no_data.append(name)

    for dev in devices_on_new:
        if dev not in devices_on_old:
            new_only.append(dev["name"])

    print(f"Migrated {sorted(migrated)}")
    print(f"Unmigrated and unswitched (possibly no longer collecting data): {sorted(unmigrated)}")
    print(f"Ready to migrate: {sorted(unmigrated_ready_to_go)}")
    # Not sure which of these is more reliable -- they generate their results using different methods
    print(f"No data: {sorted(no_data)}")
    print(f"No data2: {sorted(no_data_2)}")
    print(f"New only, nothing to do: {sorted(new_only)}")

    exit()


def print_item_counts(old_client, new_client, nums):
    print("Counting records for devies on old and new servers...")
    for num in nums:
        name = birdhouse_utils.make_device_name(num)
        cmd = f'{POSTGRES_COMMAND} "select count(*) from ts_kv where entity_id in (select id from device where name =\'{name}\');"'
        old_count = run_command(old_client, cmd).strip()
        new_count = run_command(new_client, cmd).strip()
        print(f"{name}: {'Same' if old_count == new_count else 'Different'} [Old | New: {old_count} | {new_count}]")


def check_for_dupes(items):
    """
    Make sure we haven't queued the same device up to be run twice
    """
    print("Checking to make sure the same device isn't queued twice")

    items.sort()
    for i in range(len(items) - 1):
        print(".", end="")
        if items[i] == items[i + 1]:
            print(f"List of devices to process contains at least one duplicate! ({items[i]})")
            exit()
    print()


def check_for_remigration(to_do_list):
    """
    Make sure we're not queueing up a device we've already migrated
    """
    tbapi_old = TbApi(birdhouse_utils.make_mothership_url(old_server_ip), thingsboard_username, thingsboard_password)
    tbapi_new = TbApi(birdhouse_utils.make_mothership_url(new_server_ip), thingsboard_username, thingsboard_password)

    old_client = create_client(old_server_ip)
    new_client = create_client(new_server_ip)

    print("Conducting remigration checks (to make sure device hasn't already been migrated to new server)")

    for num in to_do_list:
        print(".", end="")
        name = birdhouse_utils.make_device_name(num)

        if not device_has_data(tbapi_old, old_client, name):
            print(f"\nThere is no data on the old server for device {name}.  Nothing to migrate.")
            exit()

        if device_has_been_migrated(tbapi_old, old_client, tbapi_new, new_client, name):
            print(f"\nDevice {name} has already been migrated.")
            exit()
    print()


def device_has_data(tbapi, client, name):
    return get_arbitrary_telemetry_date(tbapi, client, name) is not None


def device_has_been_migrated(tbapi_old, old_client, tbapi_new, new_client, name):
    """
    Returns true if device has already been migrated; we know it has if we can find data with the same timestamp on both machines
    """
    timestamp = get_arbitrary_telemetry_date(tbapi_old, old_client, name)

    if timestamp is None:       # No data on source machine; no need to migrate
        return True

    return has_telmetry_for_timestamp(tbapi_new, name, timestamp)


def ensure_devices_exist(device_nums):
    """
    Ensure all devices we're going to process exist on both the source and destination machines
    """
    tbapi_old = TbApi(birdhouse_utils.make_mothership_url(old_server_ip), thingsboard_username, thingsboard_password)
    tbapi_new = TbApi(birdhouse_utils.make_mothership_url(new_server_ip), thingsboard_username, thingsboard_password)

    print("Making sure every device we want to migrate exists on both source and desitnation machines")

    for num in device_nums:
        print(".", end="")

        name = birdhouse_utils.make_device_name(num)

        old_device = tbapi_old.get_device_by_name(name)
        if old_device is None:
            print(f"\nDevice {name} does not exist on old server!")
            exit()

        new_device = tbapi_new.get_device_by_name(name)
        if new_device is None:
            print(f"\nDevice {name} does not exist on new server!")
            exit()

        old_key = tbapi_old.get_id(old_device)
        new_key = tbapi_new.get_id(new_device)

        if old_key != new_key:
            print("\nDevice keys are different on old and new servers!")
            exit()
    print()


def verify_devices_remapped(device_nums, min_interval=120, age_considered_offline=7 * DAYS):        # In seconds
    """
    Look at the ages of the most recent telemetry, and ensure each device is sending current telemetry to the new server.
    The theory is that once a device is talking to the new server, it will never go back to the old, and we can safely
    shift all telemetry from the old server.

    min_interval is the minimum time, in seconds, that the new server must be ahead of the old server for this check to pass.

    Note that if the most recent telemetry on the old server is older than age_considered_offline, we'll assume the device is out of contact
    (perhaps disconnceted or off), and we'll go ahead and migrate the data over.
    """
    tbapi_old = TbApi(birdhouse_utils.make_mothership_url(old_server_ip), thingsboard_username, thingsboard_password)
    tbapi_new = TbApi(birdhouse_utils.make_mothership_url(new_server_ip), thingsboard_username, thingsboard_password)

    old_client = create_client(old_server_ip)
    new_client = create_client(new_server_ip)

    print("Verifying devices have attached themselves to the new server and aren't still sending data to the old")


    now = int(time.time() * 1000)

    for num in device_nums:
        print(".", end="")

        name = birdhouse_utils.make_device_name(num)
        latest_old = get_latest_telemetry_date(tbapi_old, old_client, name) or 0

        if latest_old == 0:     # There is no data on the old server; no need to migrate, but no problem moving forward, either
            continue

        latest_new = get_latest_telemetry_date(tbapi_new, new_client, name) or 0

        age_of_old = int((now - latest_old) / 1000)     # Age of most recent telemetry on old server in seconds

        diff = int((latest_new - latest_old) / 1000)    # Diffrence in ages between most recent telmetry on new and old server, in seconds

        if age_of_old < age_considered_offline and diff < min_interval:
            print(f"\nIt looks like device {name} is still active and hasn't switched to new server yet.  Not ready to migrate data!")
            print(f"\tLast telemetry on old server was {format_time_delta(age_of_old)} ago.")
            if latest_new == 0:
                print("\tThe device has not yet sent any data to the new server.")
            else:
                print(f"\tLast telemetry on new server was {format_time_delta(now - latest_new)} ago.")
            exit()
    print()


def format_time_delta(seconds):
    sign_string = '-' if seconds < 0 else ''
    seconds = abs(int(seconds))
    weeks, seconds = divmod(seconds, WEEKS)
    days, seconds = divmod(seconds, DAYS)
    hours, seconds = divmod(seconds, HOURS)
    minutes, seconds = divmod(seconds, MINUTES)

    if weeks > 0:
        return f"{sign_string}{weeks} weeks, {days} days, {hours} hours, {minutes} mins, {seconds} secs"
    if days > 0:
        return f"{sign_string}{days} days, {hours} hours, {minutes} mins, {seconds} secs"
    elif hours > 0:
        return f"{sign_string}{hours} hours, {minutes} mins, {seconds} secs"
    elif minutes > 0:
        return f"{sign_string}{minutes} mins, {seconds} secs"
    else:
        return f"{sign_string}{seconds} secs"


def get_latest_telemetry_date(tbapi, client, name, key="freeHeap"):      # freeHeap is sent with every packet
    """
    Gets timestamp of most recent telemetry on server; Similar to but slower than get_arbitrary_telemetry_date
    """
    return get_arbitrary_telemetry_date(tbapi, client, name, key, get_max=True)


def get_arbitrary_telemetry_date(tbapi, client, name, key="freeHeap", get_max=False):      # freeHeap is sent with every packet
    """
    Gets timestamp of most recent telemetry on server
    """
    device = tbapi.get_device_by_name(name)
    if device is None:
        print(f"\nDevice {name} doesn't exist in the server's device table.  Please verify it has been deployed.")
        exit()

    tel = tbapi.get_latest_telemetry(device, key)

    if tel[key][0]['value'] is not None:
        return tel[key][0]['ts']

    # We didn't find any data the easy way; let's check the hard way.  We might have data that never made it to ts_kv_latest; we need to bypass by going straight to the server
    ts = run_command(client, f'{POSTGRES_COMMAND} "select {"max(ts)" if get_max else "ts"} from ts_kv where entity_id in (select id from device where name =\'{name}\') limit 1;"').strip()
    return int(ts) if ts != "" else None


def has_telmetry_for_timestamp(tbapi, name, timestamp):
    """
    Uses Thingsboard API to determine if device has a bit of telemetry for the specified timestamp.  Returns True if it does.
    """
    device = tbapi.get_device_by_name(name)
    if device is None:
        print(f"Can't find device {name}...")
        exit()
    tel = tbapi.get_telemetry(device, "freeHeap", startTime=timestamp - 1, endTime=timestamp + 1)    # freeHeap is sent with every packet

    return len(tel) > 0 and tel['freeHeap'][0]['value'] is not None


def has_telemetry(tbapi, device):
    """
    Determines if device telemetry on machine pointed to by tbapi.  Looks at ts_kv, not at ts_kv_latest; assume pressure will always be part of the data package
    """
    try:
        tel = tbapi.get_telemetry(device, "pressure", 0, time.time() * 1000, limit=1)
    except requests.exceptions.HTTPError as ex:
        if ex.response.status_code == 404 and ex.response.text == "Device with requested id wasn't found!":
            return False
        raise

    return len(tel) > 0


def create_client(addr):
    """
    Manages creation of paramiko ssh clients
    """
    client = paramiko.SSHClient()
    client.load_system_host_keys()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy)

    client.connect(addr, port=SSH_PORT, username=ssh_username, password=ssh_password)

    return client


def run_command(client, command):
    stdin, stdout, stderr = client.exec_command(command)
    return stdout.read().decode('UTF-8')


def load_data(client, filename, num):
    """
    Load data from filename into thingsboard database on remote server
    """
    outfile = f"dataload_{num}.out"
    print(f"Loading data from {filename} into postgres...", end="")
    out = run_command(client, f'nohup {POSTGRES_COMMAND} "\\copy ts_kv ({COLUMN_LIST}) from \'{filename}\' DELIMITER \',\' null \'\\N\' csv;" > {outfile}')
    print(" done.")
    if out.strip() != "":
        print(f"\t[{out}]")

    out2 = run_command(client, f"cat {outfile} & rm {outfile}")
    if out2.strip() != "":
        print(f"\t[{out2}]")


def transfer_file(client, source_ip, source_file, md5):
    """
    Copy a file from server specified by source_ip to the remote server specified by client.  Presumes that token login works, of course.
    """
    print("Checking if file has already been copied...", end="")
    newmd5 = get_md5(client, source_file)
    if md5 == newmd5:
        print(" it has!  Yay!")
        return
    print(" no.")

    print("Copying data to new server...", end="")
    copycmd = f"scp {source_ip}:{source_file} {source_file}"
    out = run_command(client, copycmd)
    print(" checking hashes...", end="")
    lines = run_command(client, f"ls {source_file} | wc -l")

    if int(lines.strip()) == 0:
        print(f"File {source_file} not copied.")
        print("Please verify that auto login is configured so the following command can run on the new server without interaction:")
        print(copycmd)
        exit()

    if out.strip() != "":
        print(out)
        exit()

    newmd5 = get_md5(client, source_file)
    if md5 != newmd5:
        print(" no match... aborting.", end="")
        exit()

    print(" match...", end="")
    print(" done.")


def get_md5(client, filename):
    """
    Return the md5 hash of a file on a remote server
    """
    return run_command(client, f"md5sum {filename} |awk '{{print $1}}'")


def delete_file(client, filename, where):
    """
    Delete file on remote server; runs as su so we can delete anything
    """
    print(f"Deleting {filename} on {where}...", end='')
    out = run_command(client, f"sudo rm {filename}")
    print(" done.")

    return out


def export_data(client, bhnum, filename):
    """
    Export data from postres server on the client to filename, in CSV format
    """
    device_name = birdhouse_utils.make_device_name(bhnum)

    command = f"""   {POSTGRES_COMMAND} "COPY (select {COLUMN_LIST} from ts_kv 
                                               where entity_id = (select id from device where name = \'{device_name}\'))
                                         TO \'{filename}\' (DELIMITER \',\');"    """

    print(f"Exporting data for {device_name} to {filename}...", end='')
    out = run_command(client, command)
    print(" done.")

    if out.strip() != "":
        print(out)
        exit()

    linect = run_command(client, f"wc -l {filename}|awk '{{print $1}}'")
    print(f"Exported {int(linect.strip()) - 1} records.")

    return filename


def report_device_info(bhnums):
    old_client = create_client(old_server_ip)
    new_client = create_client(new_server_ip)

    new = count_records(new_client, bhnums)
    old = count_records(old_client, bhnums)

    for k in new.keys():
        if k in old and k in new:
            print(f"{k}: Old: {old[k][0]:,d}, New: {new[k][0]:,d} (diff: {new[k][0] - old[k][0]:,d}), dates:({old[k][1]:%d-%m-%Y} - {old[k][2]:%d-%m-%Y},{new[k][1]:%d-%m-%Y} - {new[k][2]:%d-%m-%Y})")
        elif k in old:
            print(f"{k} Only in old, {old[k][0]:,d}, {old[k][1]:%d-%m-%Y} - {old[k][2]:%d-%m-%Y}")
        elif k in new:
            print(f"{k} Only in new, {new[k][0]:,d}, {new[k][1]:%d-%m-%Y} - {new[k][2]:%d-%m-%Y}")
        else:
            print(f"{k} has no data anywhere")


def count_records(client, bhnums):
    device_names = [birdhouse_utils.make_device_name(n) for n in bhnums]

    comma = "','"
    command = f'{POSTGRES_COMMAND} "select device.name, count(*), min(ts), max(ts) from ts_kv join device on ts_kv.entity_id = device.id where entity_id in (select id from device where name in (\'{comma.join(device_names)}\')) group by device.name order by device.name"'
    log.info(f"Command: {command}")

    print(f"Counting records for {', '.join(device_names)}...", end='')
    out = run_command(client, command).strip().split("\n")
    print(" done.")

    stats = dict()
    log.info(f"Output: {out}")

    for o in out:
        if o.strip() == "":
            continue

        bhnum, count, min_ts, max_ts = o.split("|")
        stats[bhnum] = int(count), datetime.datetime.fromtimestamp(int(min_ts) / 1000), datetime.datetime.fromtimestamp(int(max_ts) / 1000)

    log.info(f"Returning: {stats}")
    return stats


main()
