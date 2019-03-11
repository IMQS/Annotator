Shapefile read/write.

Regarding Endianness inside the code: We only treat big-endian numbers specially, since
I assume this is only going to be running on little-endian hardware.

Throughout the code I use size_t instead of 64-bit integers to reference file positions.
This is for speed on 32-bit systems, on which one is very unlikely ever to use a shapefile
larger than 4 gigabytes. On 64-bit systems, we will be able to deal with shapefiles up to
8 gigabytes, which is the specification-imposed limit (due to their use of unsigned 32 bit
indices, that reference 16 bit units instead of the usual 8 bit units).


Some notes about Arcview
------------------------

In arcview, when you delete features in a shapefile, it actually purges them. I think the null
feature thing is simply an in-process mechanism.