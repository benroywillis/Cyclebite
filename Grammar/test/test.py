# runs the suite of test cases (in "Cases/") and interprets the results for correctness
import argparse
import subprocess
import os
import re
import collections

# global parameters to define the space of test cases available
stages = [ "IV", "RV", "BP" ]
cases  = { "GEMM": ["OPFLAG_O0_DEBUG_G0", "OPFLAG_O3_DEBUG_G0"] }

# for each breakout step in the Grammar pipeline
#    run it
#    re its print
#    compare the results to the known correct results
#    remember the result

def parseArgs():
	arg_parser = argparse.ArgumentParser()
	arg_parser.add_argument("-s", "--stage", default="all", help="Define stage to test (e.g., IV, RV). Defaults to all.")
	arg_parser.add_argument("-c", "--case", default="GEMM", help="Define case name (e.g., GEMM, ElementMultiply")
	arg_parser.add_argument("-v", "--variant", default="OPFLAG_O3_DEBUG_G0", help="Define case variant (e.g., OPFLAG_O3_DEBUG_G0")
	args = arg_parser.parse_args()
	return args

def flatten(input):
	if isinstance(input, collections.Iterable):
		for entry in input:
			if isinstance(entry, collections.Iterable) and not isinstance(entry, (str, bytes)):
				yield from flatten(entry)
			else:
				yield entry
	else:
		yield entry

def getTaskOutput(output):
	"""
	Bunches together the output of each task
	"""
	# maps task names to their output lines
	taskOutputs = {}
	# flag states when task output is being recorded
	record = False
	# keeps the name of the task that is being recorded
	currentTask = ""
	for line in output.split("\n"):
		newTask = re.findall("Task\s\d+", line)
		if len(newTask):
			currentTask = newTask[0]
			if taskOutputs.get(currentTask) is None:
				taskOutputs[currentTask] = []
			continue
		# if a task is being recorded put this line in its output space
		if len(currentTask):
			taskOutputs[currentTask].append(line)
	return taskOutputs

def readOutput(output, stage, case, variant):
	if case == "GEMM":
		if variant == "OPFLAG_O0_DEBUG_G0":
			# should have 2 tasks: a GEMM task (task0) and an input rand() task (task1)
			taskOutputs = getTaskOutput(output)
			if len(taskOutputs) != 2:
				raise Exception("Did not find 2 tasks in case "+case+", variant "+variant+"; found "+str(len(taskOutputs)))
			if stage == "IV":
				# task0: 3 IVs %9 %8 and %7 (in that order)
				# task1: 2 IVs %6 and %5 (in that order)
				valuesToFind = { "%9", "%8", "%7" }
				for entry in taskOutputs["Task 0"]:
					if "->" in entry:
						values = re.findall("%\d+", entry)
						if values[0] in valuesToFind:
							valuesToFind.remove(values[0])
				if len(valuesToFind):
					raise Exception("Did not find values "+str(valuesToFind)+" in the IVs of case "+case+", variant "+variant+", Task 0!")
				valuesToFind = { "%6", "%5" }
				for entry in taskOutputs["Task 1"]:
					if "->" in entry:
						values = re.findall("%\d+", entry)
						if values[0] in valuesToFind:
							valuesToFind.remove(values[0])
				if len(valuesToFind):
					raise Exception("Did not find values "+str(valuesToFind)+" in the IVs of case "+case+", variant "+variant+", Task1!")
				return "Pass"
			elif stage == "RV":
				for entry in taskOutputs["Task 0"]+taskOutputs["Task 1"]:
					if "->" in entry:
						raise Exception("Found an RV where there shouldn't be one in case "+case+", variant "+variant+"!")
				return "Pass"
			elif stage == "BP": 
				# task 0: none
				# task 1: %12 %16 %20
				bpsFound = { "%12": 0, "%16": 0, "%20": 0 }
				for entry in taskOutputs["Task 0"]+taskOutputs["Task 1"]:
					if "->" in entry:
						if "%12" in entry:
							bpsFound["%12"] += 1
						elif "%16" in entry:
							bpsFound["%16"] += 1
						elif "%20" in entry:
							bpsFound["%20"] += 1
				if (bpsFound["%12"] != 1) or (bpsFound["%16"] != 1) or (bpsFound["%20"] != 1):
					raise Exception("Did not find the correct base pointers in case "+case+", variant "+variant+"!")
				return "Pass"
			elif stage == "COLL":
				# task 0: 

			else:
				print("Stage name "+stage+" for test case "+case+" and variant "+variant+" not recognized")
		elif variant == "OPFLAG_O3_DEBUG_G0":
			# should have 2 tasks: a GEMM task (task0) and an input rand() task (task1)
			taskOutputs = getTaskOutput(output)
			if len(taskOutputs) != 2:
				raise Exception("Did not find 2 tasks in case "+case+", variant "+variant+"; found "+str(len(taskOutputs)))
			if stage == "IV":
				# should have 2 tasks: an input rand() task (task0) and a GEMM task (task1)
				# task 0: 3 IVs %38 %51 and %40
				# task 1: 2 IVs %71 and %29
				valuesToFind = { "%71", "%29" }
				for entry in taskOutputs["Task 1"]:
					if "->" in entry:
						values = re.findall("%\d+", entry)
						if values[0] in valuesToFind:
							valuesToFind.remove(values[0])
				if len(valuesToFind):
					raise Exception("Did not find values "+str(valuesToFind)+" in the IVs of case "+case+", variant "+variant+", Task 0!")
				valuesToFind = { "%38", "%51", "%40" }
				for entry in taskOutputs["Task 0"]:
					if "->" in entry:
						values = re.findall("%\d+", entry)
						if values[0] in valuesToFind:
							valuesToFind.remove(values[0])
				if len(valuesToFind):
					raise Exception("Did not find values "+str(valuesToFind)+" in the IVs of case "+case+", variant "+variant+", Task1!")
				return "Pass"

			elif stage == "RV":
				# task 0: %57
				# task 1: None
				found = False
				for entry in taskOutputs["Task 0"]+taskOutputs["Task 1"]:
					if "->" in entry:
						if "%57" in entry:
							found = True
				if not found:
					raise Exception("Could not find the RV in case "+case+", variant "+variant+"!")
				return "Pass"
			elif stage == "BP": 
				# task 0: %8 %15 %22
				# task 1: %8 %15 %22
				bpsFound = { "%8": 0, "%15": 0, "%22": 0 }
				for entry in taskOutputs["Task 0"]+taskOutputs["Task 1"]:
					if "->" in entry:
						if "%8" in entry:
							bpsFound["%8"] += 1
						elif "%15" in entry:
							bpsFound["%15"] += 1
						elif "%22" in entry:
							bpsFound["%22"] += 1
				if (bpsFound["%8"] != 2) or (bpsFound["%15"] != 2) or (bpsFound["%22"] != 2):
					raise Exception("Did not find the correct base pointers in case "+case+", variant "+variant+"!")
				return "Pass"
			else:
				print("Stage name "+stage+" for test case "+case+" and variant "+variant+" not recognized")
		else:
			print("Variant name "+variant+" for test case "+case+" not recognized")
	else:
		print("Case name "+case+" not recognized")
	return -1

def runStage(stage, case, variant):
	"""
	@param[in] stage	The Cyclebite::Grammar pipeline stage to be run (e.g., IV, RV, BP)
	@param[in] case		The application name of the test case to run. These are the folders in the "Cases/" folder. (e.g., GEMM)
	@param[in] variant 	The variant name of the test case to run. These are the folders in the "Cases/CASE_NAME" folder. (e.g., CASE_NAME=GEMM,CASE_VARIANT=OPFLAG_O0_DEBUG_G0)
	"""
	#command = "cd "+stage+" ; make clean ; make run CASE_NAME="+case+" CASE_VARIANT="+variant
	command = "cd "+stage+" ; make run CASE_NAME="+case+" CASE_VARIANT="+variant
	proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
	proc.stdout.readline()
	try:
		standardout = proc.communicate()
	except Exception as e:
		print("Encountered error when communicating with stage \""+stage+"\": "+str(e))
		return -1
	return "".join(x.decode("utf-8") for x in flatten(standardout) )

def runTests(args):
	"""
	@param[in] cases	Maps case names to their variants (e.g., { "name": [ "variant0", "variant1", ...] } )
	"""
	if args.stage != "all":
		raise ValueError("Cannot yet support running specific stages")
	results = {}
	for stage in stages:
		results[stage] = {}
		for case in cases:
			results[stage][case] = {}
			for v in cases[case]:
				results[stage][case][v] = "NoResult"
				try:
					out = runStage(stage, case, v)
					if out == -1:
						results[stage][case][v] = "Fail"
						raise ValueError("Error encountered when running stage "+stage+", case "+case+", variant "+v+"!")
					out = readOutput(out, stage, case, v)
					if out == -1:
						results[stage][case][v] = "Fail"
						raise ValueError("Error encountered when reading the output of stage "+stage+", case "+case+", variant "+v+"!")
					results[stage][case][v] = out
				except Exception as e:
					print(str(e))
					continue
	return results

args = parseArgs()
print(runTests(args))
