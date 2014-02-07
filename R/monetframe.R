# Adapted from MonetDB.R monet.frame code

# this wraps a sql database (in particular MonetDB) with a DBI connector 
# to have it appear like a data.frame

# shorthand constructor, also creates connection to db
mirf <- function(database,table,debug=FALSE) {
	#dburl <- paste0("monetdb://",host,":",port,"/",database)	
	con <- dbConnect(MonetinR(), database)
	monetinr.frame(con,table,debug)
}

# can either be given a query or simply a table name
# now supports hints on table structure to speed up initialization
monetinr.frame <- monetinrframe <- function(conn,tableOrQuery,debug=FALSE)
{
	if(missing(conn)) stop("'conn' must be specified")
	if(missing(tableOrQuery)) stop("a sql query or a table name must be specified")
	
	obj = new.env()
	class(obj) = "monet.frame"
	attr(obj,"conn") <- conn
	query <- tableOrQuery
	
	if (length(grep("^SELECT.*",tableOrQuery,ignore.case=TRUE)) == 0) {
		query <- paste0("SELECT * FROM ",make.db.names(conn,tableOrQuery,allow.keywords=FALSE))
	}
	
	attr(obj,"query") <- query
	attr(obj,"debug") <- debug
	
	if (debug) cat(paste0("QQ: '",query,"'\n",sep=""))	
	# do this here, in case the nrow thing needs it
	coltestquery <- gsub("SELECT (.*?) FROM (.*?) (ORDER|LIMIT|OFFSET).*","SELECT \\1 FROM \\2",query,ignore.case=TRUE)
	
	# strip away things the prepare does not like
	coltestquery <- gsub("SELECT (.*?) FROM (.*?) (ORDER|LIMIT|OFFSET).*","SELECT \\1 FROM \\2",query,ignore.case=TRUE)
		
	# get column names and types from prepare response
	res <- dbGetQuery(conn, paste0(coltestquery, " LIMIT 1"))
	attr(obj,"cnames") <- names(res)
	attr(obj,"ncol") <- length(res)
	attr(obj,"rtypes") <- sapply(res, typeof)
		
	if (debug) cat(paste0("II: 'Re-Initializing column info.'\n",sep=""))	
	
	# get result set length by rewriting to count(*), should be much faster
	# temporarily remove offset/limit, as this screws up counting
	counttestquery <- sub("(SELECT )(.*?)( FROM.*)","\\1COUNT(*)\\3",coltestquery,ignore.case=TRUE)
	nrow <- dbGetQuery(conn,counttestquery)[[1, 1]] - .getOffset(query)
	
	limit <- .getLimit(query)
	if (limit > 0) nrow <- min(nrow,limit)
	if (nrow < 1) 
		warning(query, " has zero-row result set.")
		
	attr(obj,"nrow") <- nrow
	if (debug) cat(paste0("II: 'Re-Initializing row count.'\n",sep=""))	
		
	return(obj)
}

.getOffset <- function(query) {
	os <- 0
	osStr <- gsub("(.*offset[ ]+)(\\d+)(.*)","\\2",query,ignore.case=TRUE)
	if (osStr != query) {
		os <- as.numeric(osStr)
	}
	os
}

.getLimit <- function(query) {
	l <- 0
	lStr <- gsub("(.*limit[ ]+)(\\d+)(.*)","\\2",query,ignore.case=TRUE)
	if (lStr != query) {
		l <- as.numeric(lStr)
	}
	l
}
