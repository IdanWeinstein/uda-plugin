#!/bin/awk -f

# Written by Avner BenHanoch
# Date: 2011-04-14

#"system","total cpu usage",,,,,,"dsk/total",,"net/total",,"memory usage",,,
#"date/time","usr","sys","idl","wai","hiq","siq","read","writ","recv","send","used","buff","cach","free" 
#  INPUT: "date/time","usr","sys","idl","wai","hiq","siq","read","writ","recv","send","used","buff","cach","free" 
# OUTPUT: "date/time","usr","sys","idl","wai","hiq","siq","read","writ","recv","send","used","buff","cach","free" 

BEGIN {
	FS = OFS = ","
	#print "# " "time", "avg-cpu", "avg-cpu-iowait" , "cluster-read-MB-per-sec", "cluster-write-MB-per-sec"
}

function print_record() {
	# NOTE: we use avearge for CPU statistics, and total for the rest 
	if (n) print prev, usr/n, sys/n, idl/n, wai/n, hiq/n, siq/n, read, writ, recv, send, used, buff, cach, free
	n = usr = sys = idl = wai = hiq = siq = read = writ = recv = send = used = buff = cach = free = 0
}

#/^#/{next}  #ignore comment lines
#NF!=7{print "ERROR: bad line: " $0; exit 1}

NF != 15 {print "# " $0; next}
$1~"^\"" {print "# " $0; next}

{
	if (prev != $1) {
		print_record()
	}

	#cpu related
	usr += $2
	sys += $3
	idl += $4
	wai += $5
	hiq += $6
	siq += $7
	
	#disk related
	read += $8
	writ += $9
	
	#network related
	recv += $10
	send += $11
	
	#memory related
	used += $12
	buff += $13
	cach += $14
	free += $15
			
	# open this line for debug
	# print "FNR="FNR, $1, $3, $4, $6, $7, "n=" n
	
	prev = $1
	n++
	
	next
}

END {
	print_record()
}
