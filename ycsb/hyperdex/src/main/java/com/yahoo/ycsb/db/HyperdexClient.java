/**
 * Copyright (c) 2011-2013, Cornell University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of HyperDex nor the names of its contributors may be
 *       used to endorse or promote products derived from this software without
 *       specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Descriptions borrowed from YCSB base. */

package org.hyperdex.ycsb;

import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.Vector;
import java.util.AbstractMap;
import java.util.regex.*;
import java.util.Date;

import com.yahoo.ycsb.DB;
import com.yahoo.ycsb.DBException;
import com.yahoo.ycsb.ByteIterator;
import com.yahoo.ycsb.StringByteIterator;
import com.yahoo.ycsb.Status;

import org.hyperdex.client.ByteString;
import org.hyperdex.client.Client;
import org.hyperdex.client.HyperDexClientException;
import org.hyperdex.client.Iterator;
import org.hyperdex.client.GreaterEqual;
import org.hyperdex.client.LessThan;
import org.hyperdex.client.Range;

public class HyperdexClient extends DB
{
    private Client m_client;
    private Pattern m_pat;
    private Matcher m_mat;
    private boolean m_scannable;
    private int m_retries;
    private int count; 
    private int print_every;

    /**
     * Initialize any state for this DB.
     * Called once per DB instance; there is one DB instance per client thread.
     */
    public void init() throws DBException
    {
//	System.out.println("HypderDex init()\n");
        String host = getProperties().getProperty("hyperdex.host", "127.0.0.1");
        Integer port = Integer.parseInt(getProperties().getProperty("hyperdex.port", "1982"));
        m_client = new Client(host, port);
        m_pat = Pattern.compile("([a-zA-Z]*)([0-9]*)");
        m_mat = m_pat.matcher("user1");
        m_scannable = getProperties().getProperty("hyperdex.scannable", "false").equals("true");
        m_retries = 10;
	System.out.println("HypderDex init() complete.");
	if (m_scannable) {
		System.out.println("HyperDex is set to be scannable.");
	}
	count = 0; 
	print_every = 100000; 
    }

    /**
     * Cleanup any state for this DB.
     * Called once per DB instance; there is one DB instance per client thread.
     */
    public void cleanup() throws DBException
    {
    }

    public void updateCount() {
	count++;
    	if (count % print_every == 0) {
//                System.out.print(new Date() + "   ");
                System.out.println(new Date() + "   " + count + " operations complete.");
        }
    }

    /**
     * Read a record from the database. Each field/value pair from the result will be stored in a HashMap.
     *
     * @param table The name of the table
     * @param key The record key of the record to read.
     * @param fields The list of fields to read, or null for all of them
     * @param result A HashMap of field/value pairs for the result
     * @return Zero on success, a non-zero error code on error or "not found".
     */
    public Status read(String table, String key, Set<String> fields, HashMap<String,ByteIterator> result)
    {
	updateCount();

        while (true)
        {
            Map map = new HashMap<String,Object>();

            try
            {
                map = m_client.get(table, key);
            }
            catch(HyperDexClientException e)
            {
                if (e.status() == 8517)
                {
                    continue;
                }

                return Status.ERROR;
            }
            catch(Exception e)
            {
                return Status.ERROR;
            }

            convert_to_java(fields, map, result);
            return Status.OK;
        }
    }

    /**
     * Perform a range scan for a set of records in the database. Each field/value pair from the result will be stored in a HashMap.
     *
     * @param table The name of the table
     * @param startkey The record key of the first record to read.
     * @param recordcount The number of records to read
     * @param fields The list of fields to read, or null for all of them
     * @param result A Vector of HashMaps, where each HashMap is a set field/value pairs for one record
     * @return Zero on success, a non-zero error code on error.  See this class's description for a discussion of error codes.
     */
    public Status scan(String table, String startkey, int recordcount, Set<String> fields, Vector<HashMap<String,ByteIterator>> result)
    {
	updateCount();
	recordcount = recordcount * 150; 
//	System.out.println("table: " + table + " startKey: " + startkey + " recordcount: " + recordcount);

        while (true)
        {
            // XXX I'm going to be lazy and not support "fields" for now.  Patches
            // welcome.

            if (!m_scannable)
            {
//		System.out.println("m_scannable is false !");
                return Status.ERROR;
            }

            m_mat.reset(startkey);

            if (!m_mat.matches())
            {
//		System.out.println("m_mat.matches() is false !");
                return Status.ERROR;
            }

            long base = Long.parseLong(m_mat.group(2));
            long lower = base << 32;
            long upper = (base + recordcount) << 32;

//	    System.out.println("count: " + count + " base: " + base + " lower: " + lower + " upper: " + upper + " recordcount: " + recordcount);

            HashMap<String,Object> values = new HashMap<String,Object>();
            AbstractMap.SimpleEntry<Long,Long> range
                = new AbstractMap.SimpleEntry<Long,Long>(lower,upper);
//            values.put("recno", range);
//	    values.put("recno", new GreaterEqual(lower)); 
//	    values.put("recno", new LessThan(upper));
	    values.put("recno", new Range(lower, upper)); 

	    //System.out.println("values: " + values.toString());
	    
	    // Hack to temporarily test with just one next
//	    recordcount = 2;

            try
            {
                Iterator s = m_client.search(table, values);
//		Iterator s = m_client.sorted_search(table, values, "recno", recordcount, false);
		int cnt = 1;

                while (s.hasNext() &&  cnt < recordcount)
                {
                    s.next();
		    cnt++;
                }

//		System.out.println("lower: " + lower + " upper: " + upper + " Number of values scanned: " + cnt);
                return Status.OK;
            }
            catch(HyperDexClientException e)
            {
		//System.out.println("HyperDexClientException: ");
		//e.printStackTrace();

                if (e.status() == 8517)
                {
                    continue;
                }

                return Status.ERROR;
            }
            catch(Exception e)
            {
//		e.printStackTrace();
                return Status.ERROR;
            }
        }
    }

    /**
     * Update a record in the database. Any field/value pairs in the specified values HashMap will be written into the record with the specified
     * record key, overwriting any existing values with the same field name.
     *
     * @param table The name of the table
     * @param key The record key of the record to write.
     * @param values A HashMap of field/value pairs to update in the record
     * @return Zero on success, a non-zero error code on error.  See this class's description for a discussion of error codes.
     */
    public Status update(String table, String key, HashMap<String,ByteIterator> _values)
    {
	updateCount();

//	System.out.println("HyperDex update() called. \n");
	//System.out.println("Update: table: " + table + " key: " + key);
        while (true)
        {
            HashMap<String,Object> values = new HashMap<String,Object>();

            for (Map.Entry<String, ByteIterator> entry : _values.entrySet())
            {
                values.put(entry.getKey(), new ByteString(entry.getValue().toArray()));
            }

            if (m_scannable)
            {
                m_mat.reset(key);

                if (!m_mat.matches())
                {
		    System.out.println("m_mat doesn't match. \n");
                    return Status.ERROR;
                }

                long num = Long.parseLong(m_mat.group(2));
	//	System.out.println("num: " + num + " recno: " + new Long(num << 32));
                values.put("recno", new Long(num << 32));
            }

//	    System.out.println("-------------------------- table: " + table + " key: " + key + " values: -------------------------");
//	    for (String k: values.keySet()) {
//	    	System.out.println(k + ": " + values.get(k).toString());
//	    }
            try
            {
//		System.out.println("Calling client.put\n");
                m_client.put(table, key, values);
                return Status.OK;
            }
            catch(HyperDexClientException e)
            {
		e.printStackTrace();
                if (e.status() == 8517)
                {
                    continue;
                }

                return Status.ERROR;
            }
            catch(Exception e)
            {
		e.printStackTrace();
                System.err.println(e.toString());
                return Status.ERROR;
            }
        }
    }

    /**
     * Insert a record in the database. Any field/value pairs in the specified values HashMap will be written into the record with the specified
     * record key.
     *
     * @param table The name of the table
     * @param key The record key of the record to insert.
     * @param values A HashMap of field/value pairs to insert in the record
     * @return Zero on success, a non-zero error code on error.  See this class's description for a discussion of error codes.
     */
    public Status insert(String table, String key, HashMap<String,ByteIterator> values)
    {
        return update(table, key, values);
    }

    /**
     * Delete a record from the database.
     *
     * @param table The name of the table
     * @param key The record key of the record to delete.
     * @return Zero on success, a non-zero error code on error.  See this class's description for a discussion of error codes.
     */
    public Status delete(String table, String key)
    {
	updateCount();

        while (true)
        {
            try
            {
                m_client.del(table, key);
                return Status.OK;
            }
            catch(HyperDexClientException e)
            {
                if (e.status() == 8517)
                {
                    continue;
                }

                return Status.ERROR;
            }
            catch(Exception e)
            {
                return Status.ERROR;
            }
        }
    }

    private void convert_to_java(Set<String> fields, Map interres, HashMap<String,ByteIterator> result)
    {
        if (fields == null)
        {
            return;
        }

        for (String key : fields)
        {
            // Q: under which condition, interres.containsKey(key) is false?
            if (interres.containsKey(key))
            {
                result.put(key, new StringByteIterator(interres.get(key).toString()));
            }
        }
    }
}

