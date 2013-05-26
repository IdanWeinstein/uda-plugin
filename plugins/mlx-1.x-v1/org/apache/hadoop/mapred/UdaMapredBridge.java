/*
** Copyright (C) 2012 Auburn University
** Copyright (C) 2012 Mellanox Technologies
** 
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at:
**  
** http://www.apache.org/licenses/LICENSE-2.0
** 
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, 
** either express or implied. See the License for the specific language 
** governing permissions and  limitations under the License.
**
**
*/

package org.apache.hadoop.mapred;

import java.io.IOException;
import org.apache.hadoop.mapred.JobConf;
import org.apache.hadoop.mapred.Task;
import org.apache.hadoop.mapred.Task.TaskReporter;
import org.apache.hadoop.mapred.ReduceTask.ReduceCopier;
import org.apache.hadoop.util.ReflectionUtils;
import org.apache.hadoop.fs.FileSystem;

/**
 * ShuffleConsumerPlugin that can serve Reducers, and shuffle MOF files from tasktrackers.
 * The tasktracker may use a matching ShuffleProviderPlugin
 * 
 * NOTE: This interface is also used when loading 3rd party plugins at runtime
 * 
 */
public class UdaMapredBridge {
	
	public static ShuffleConsumerPlugin getShuffleConsumerPlugin(Class<? extends ShuffleConsumerPlugin> clazz, ReduceTask reduceTask, 
			TaskUmbilicalProtocol umbilical, JobConf conf, Reporter reporter) throws ClassNotFoundException, IOException  {
	
		return ShuffleConsumerPlugin.getShuffleConsumerPlugin(clazz, reduceTask, umbilical, conf, (TaskReporter) reporter);
	}

}
