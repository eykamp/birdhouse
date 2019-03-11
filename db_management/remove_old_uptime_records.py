#!/usr/bin/python
import psycopg2  # pip install psycopg2-binary
import logging
import sys

# Run as user postgres

"""
This script will delete any uptime data more than 30 days old, except those records that are written either:
    1) Immediately after a reboot
    2) Immediately prior to a device rebooting or being taken offline

To be run daily via cron    
"""

logging.basicConfig(filename='/var/log/db_maintenance.log', level=logging.DEBUG, format='%(asctime)s %(levelname)s %(message)s', datefmt='%d %b %Y %H:%M:%S')

con = None
rows = -1


con = psycopg2.connect("dbname='thingsboard'")

def main():
    try:
        logging.info("Starting daily maintenance")
        cur = con.cursor()
        cur.execute("""
            WITH deletable AS (

            SELECT t.*
            FROM (  SELECT *,
                        lead(long_v) over (partition by entity_id order by ts) as next_val,
                        lag(long_v) over (partition by entity_id order by ts) as prev_val
                    FROM ts_kv
                    WHERE key = 'uptime'
                            AND 
                            ts < trunc(extract(epoch from (now() - interval '1 month')) * 1000)       -- Older than a month
                ) AS t

            WHERE long_v < next_val  -- delete where the next value is higher than this one... still growing!
                    AND              -- but only where...
                    long_v > prev_val limit 200000  -- we are not the lowest 
            )
            
            DELETE FROM ts_kv K
            USING deletable
            WHERE K.entity_id = deletable.entity_id  AND  K.key = 'uptime'  AND  K.ts = deletable.ts;
            """)

        rows = cur.rowcount
        con.commit()
        logging.info("Deleted " + str(rows) + " uptime records")

        vacuum("ts_kv")

        logging.info("Vacuum finished")

    except psycopg2.DatabaseError as e:
        logging.error("Error removing useless uptime records: %s" % e)
        
    finally:
        if con:
            con.close()


# Adapted from https://nessy.info/?p=886
def vacuum(table):
    """
    Run vacuum on specified table
    """

    query = "VACUUM %s" % table


    # VACUUM can not run in a transaction block,
    # which psycopg2 uses by default.
    # http://bit.ly/1OUbYB3
    isolation_level = con.isolation_level
    con.set_isolation_level(0)

    cur = con.cursor()
    cur.execute(query)

    # Restore isolation_level
    con.set_isolation_level(isolation_level)

    return con.notices


main()
