import sys

def readFile (filename):
	f=open(filename, "rb")
	lines = f.readlines()
	f.close()
	return (lines)

filename = sys.argv[1]

original=readFile (filename)
copy=readFile("copy_"+filename)
print("Original_size :", len(original))
print("Copy_size :", len(copy))

#Comparaison
k=0
while (k<len(original) and original[k]==copy[k]):
	k+=1
	
if k==len(original):
	print("Files are the same")
else:
	print("Error at line ", k)
	print("Original :"+str(original[k]))
	print("Copy :"+str(copy[k]))
