/**                                                                                                                                                                                
 * Copyright (c) 2010 Yahoo! Inc. All rights reserved.                                                                                                                             
 *                                                                                                                                                                                 
 * Licensed under the Apache License, Version 2.0 (the "License"); you                                                                                                             
 * may not use this file except in compliance with the License. You                                                                                                                
 * may obtain a copy of the License at                                                                                                                                             
 *                                                                                                                                                                                 
 * http://www.apache.org/licenses/LICENSE-2.0                                                                                                                                      
 *                                                                                                                                                                                 
 * Unless required by applicable law or agreed to in writing, software                                                                                                             
 * distributed under the License is distributed on an "AS IS" BASIS,                                                                                                               
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or                                                                                                                 
 * implied. See the License for the specific language governing                                                                                                                    
 * permissions and limitations under the License. See accompanying                                                                                                                 
 * LICENSE file.                                                                                                                                                                   
 */
package org.hyperdex.ycsb;

import com.yahoo.ycsb.*;
import java.io.*;
import java.util.HashMap;
import java.util.Properties;
import java.util.Set;
import java.util.Enumeration;
import java.util.Vector;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.LockSupport;

/**
 * Basic DB that just prints out the requested operations, instead of doing them against a database.
 */
public class TraceRecorder extends DB {
	public static final String VERBOSE="recorder.verbose";
	public static final String VERBOSE_DEFAULT="false";
	public static final String TRACEFILE="recorder.file";
	public static final String TRACEFILE_DEFAULT=null;
    	boolean verbose;
	PrintWriter traceout;
	BufferedWriter traceoutbuf;

	public TraceRecorder() {}

	/**
	 * Initialize any state for this DB.
	 * Called once per DB instance; there is one DB instance per client thread.
	 */
	@SuppressWarnings("unchecked")
	public void init() {
		verbose = Boolean.parseBoolean(getProperties().getProperty(VERBOSE, VERBOSE_DEFAULT));
		String tracefile = getProperties().getProperty(TRACEFILE, TRACEFILE_DEFAULT);
		try {
			traceoutbuf = new BufferedWriter(new FileWriter(tracefile), 4096 * 1024);
			traceout = new PrintWriter(traceoutbuf);
		} catch (Exception e) {
			e.printStackTrace();
			System.exit(1);
		}
		if (verbose) {
			System.out.println("***************** properties *****************");
			System.out.println("tracefile = " + tracefile);
			Properties p=getProperties();
			if (p!=null) {
				for (Enumeration e=p.propertyNames(); e.hasMoreElements(); ) {
					String k=(String)e.nextElement();
					System.out.println("\""+k+"\"=\""+p.getProperty(k)+"\"");
				}
			}
			System.out.println("**********************************************");
		}
	}

	public void cleanup() {
		try {
			traceoutbuf.flush();
		} catch (Exception e) {
			e.printStackTrace();
			System.exit(1);
		}
	}

	void writetrace(String s) {
		try {
			traceout.println(s);
		} catch (Exception e) {
			e.printStackTrace();
			System.exit(1);
		}
	}

	/**
	 * Read a record from the database. Each field/value pair from the result will be stored in a HashMap.
	 *
	 * @param table The name of the table
	 * @param key The record key of the record to read.
	 * @param fields The list of fields to read, or null for all of them
	 * @param result A HashMap of field/value pairs for the result
	 * @return Zero on success, a non-zero error code on error
	 */
	public Status read(String table, String key, Set<String> fields, HashMap<String,ByteIterator> result) {
		if (verbose) {
			System.out.print("READ "+table+" "+key+" [ ");
			if (fields!=null) {
				for (String f : fields) {
					System.out.print(f+" ");
				}
			} else {
				System.out.print("<all fields>");
			}

			System.out.println("]");
		}
		assert table.equals("usertable");
		assert fields == null;
		assert key.startsWith("user");
		writetrace("r " + key.substring(4));
		return Status.OK;
	}
	
	/**
	 * Perform a range scan for a set of records in the database. Each field/value pair from the result will be stored in a HashMap.
	 *
	 * @param table The name of the table
	 * @param startkey The record key of the first record to read.
	 * @param recordcount The number of records to read
	 * @param fields The list of fields to read, or null for all of them
	 * @param result A Vector of HashMaps, where each HashMap is a set field/value pairs for one record
	 * @return Zero on success, a non-zero error code on error
	 */
	public Status scan(String table, String startkey, int recordcount, Set<String> fields, Vector<HashMap<String,ByteIterator>> result)
	{
		if (verbose) {
			System.out.print("SCAN "+table+" "+startkey+" "+recordcount+" [ ");
			if (fields!=null) {
				for (String f : fields)	{
					System.out.print(f+" ");
				}
			} else {
				System.out.print("<all fields>");
			}

			System.out.println("]");
		}

		assert table.equals("usertable");
		assert fields == null;
		assert startkey.startsWith("user");
		writetrace("s " + startkey.substring(4) + " " + recordcount);

		return Status.OK;
	}

	private int maplen(HashMap<String,ByteIterator> values) {
		int result = 0;
		for (String k : values.keySet()) {
			result += k.length() + values.get(k).bytesLeft();
		}
		return result;
	}

	/**
	 * Update a record in the database. Any field/value pairs in the specified values HashMap will be written into the record with the specified
	 * record key, overwriting any existing values with the same field name.
	 *
	 * @param table The name of the table
	 * @param key The record key of the record to write.
	 * @param values A HashMap of field/value pairs to update in the record
	 * @return Zero on success, a non-zero error code on error
	 */
	public Status update(String table, String key, HashMap<String,ByteIterator> values) {
		if (verbose) {
			System.out.print("UPDATE "+table+" "+key+" [ ");
			if (values!=null) {
				for (String k : values.keySet()) {
					System.out.print(k+"="+values.get(k)+" ");
				}
			}
			System.out.println("]");
		}

		assert table.equals("usertable");
		assert key.startsWith("user");
		writetrace("u " + key.substring(4) + " " + maplen(values));
		return Status.OK;
	}

	/**
	 * Insert a record in the database. Any field/value pairs in the specified values HashMap will be written into the record with the specified
	 * record key.
	 *
	 * @param table The name of the table
	 * @param key The record key of the record to insert.
	 * @param values A HashMap of field/value pairs to insert in the record
	 * @return Zero on success, a non-zero error code on error
	 */
	public Status insert(String table, String key, HashMap<String,ByteIterator> values) {
		if (verbose) {
			System.out.print("INSERT "+table+" "+key+" [ ");
			if (values!=null) {
				for (String k : values.keySet()) {
					System.out.print(k+"="+values.get(k)+" ");
				}
			}

			System.out.println("]");
		}

		assert table.equals("usertable");
		assert key.startsWith("user");
		writetrace("i " + key.substring(4) + " " + maplen(values));
		return Status.OK;
	}


	/**
	 * Delete a record from the database. 
	 *
	 * @param table The name of the table
	 * @param key The record key of the record to delete.
	 * @return Zero on success, a non-zero error code on error
	 */
	public Status delete(String table, String key) {
		if (verbose) {
			System.out.println("DELETE "+table+" "+key);
		}

		assert table.equals("usertable");
		assert key.startsWith("user");
		writetrace("d " + key.substring(4));

		return Status.OK;
	}
}
