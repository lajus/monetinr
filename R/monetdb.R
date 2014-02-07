require(DBI)
require(MonetDB.R)

# TODO: make these values configurable in the call to dbConnect
DEBUG_IO      <- FALSE
DEBUG_QUERY   <- FALSE

# Make S4 aware of S3 classes
setOldClass(c("sockconn","connection"))

### MonetinRDriver
setClass("MonetinRDriver", contains="DBIDriver")

# allow instantiation of this driver with MonetDB to allow existing programs to work
MonetinR <- function() {
	new("MonetinRDriver")
}

setMethod("dbGetInfo", "MonetinRDriver", def=function(dbObj, ...)
		list(name="MonetinRDriver", 
			driver.version="0.1.2",
			DBI.version="0.2-5",
			client.version=NA,
			max.connections=1)
)

setMethod("dbConnect", "MonetinRDriver", def=function(drv, dir, ..., debug_kernel=0) {
		init(dir, debug_kernel)
		return(new("MonetinRConnection"))
	},
	valueClass="MonetinRConnection")


### MonetinRConnection
setClass("MonetinRConnection", contains="MonetDBConnection")

setMethod("dbDisconnect", "MonetinRConnection", def=function(conn, ..., force=FALSE) {
		stop(force)
	})

setMethod("dbGetException", "MonetinRConnection", def=function(conn, ...) {
		stop("Exceptions are automatically dumped in the R shell")
	})

setMethod("dbGetQuery", signature(conn="MonetinRConnection", statement="character"),  def=function(conn, statement, ...) {
		if (DEBUG_IO)  {
			cat("MAL plan:\n")
			explain(statement) }
		return(query(statement))
	})

# This one does all the work in this class
setMethod("dbSendQuery", signature(conn="MonetinRConnection", statement="character"),  def=function(conn, statement, ..., nowarn=FALSE) {
		if (DEBUG_IO)  {
			cat("MAL plan:\n")
			explain(statement) }
		if (!nowarn) warning("dbSendQuery: the query will be performed but the fetch function is not implemented, use dbGetQuery instead")
                invisible(query(statement))
	})


# adapted from RMonetDB, very useful...
setMethod("dbWriteTable", "MonetinRConnection", def=function(conn, name, value, overwrite=TRUE, ...) {
		if (is.vector(value) && !is.list(value)) value <- data.frame(x=value)
		if (length(value)<1) stop("value must have at least one column")
		if (is.null(names(value))) names(value) <- paste("V",1:length(value),sep='')
		if (length(value[[1]])>0) {
			if (!is.data.frame(value)) value <- as.data.frame(value, row.names=1:length(value[[1]]))
		} else {
			if (!is.data.frame(value)) value <- as.data.frame(value)
		}
		fts <- sapply(value, dbDataType, dbObj=conn)
		
		if (dbExistsTable(conn, name)) {
			if (overwrite) dbRemoveTable(conn, name)
			else stop("Table `",name,"' already exists")
		}
		
		fdef <- paste(make.db.names(conn,names(value),allow.keywords=FALSE),fts,collapse=',')
		qname <- make.db.names(conn,name,allow.keywords=FALSE)
		ct <- paste("CREATE TABLE ",qname," (",fdef,")",sep= '')
		dbSendUpdate(conn, ct)
		
		if (length(value[[1]])) {
			inss <- paste("INSERT INTO ",qname," VALUES(", paste(rep("?",length(value)),collapse=','),")",sep='')
			
			dbSendQuery(conn,"START TRANSACTION", nowarn=TRUE)
			for (j in 1:length(value[[1]])) dbSendUpdate(conn, inss, list=as.list(value[j,]))
			dbSendQuery(conn,"COMMIT", nowarn=TRUE)
		}
		TRUE
	})

# for compatibility with RMonetDB (and dbWriteTable support), we will allow parameters to this method, but will not use prepared statements internally
if (is.null(getGeneric("dbSendUpdate"))) setGeneric("dbSendUpdate", function(conn, statement,...) standardGeneric("dbSendUpdate"))
setMethod("dbSendUpdate", signature(conn="MonetinRConnection", statement="character"),  def=function(conn, statement, ..., list=NULL, async=FALSE) {
		if(async) { warning("async argument is not supported, ignoring argument") }
		if(!is.null(list) || length(list(...))){
			if (length(list(...))) statement <- .bindParameters(statement, list(...))
			if (!is.null(list)) statement <- .bindParameters(statement, list)
		}
		res <- dbSendQuery(conn,statement,nowarn=TRUE)
		TRUE
	})

.bindParameters <- function(statement,param) {
	for (i in 1:length(param)) {
		value <- param[[i]]
		valueClass <- class(value)
		if (is.na(value)) 
			statement <- sub("?","NULL",statement,fixed=TRUE)
		else if (valueClass %in% c("numeric","logical","integer"))
			statement <- sub("?",value,statement,fixed=TRUE)
		else if (valueClass == c("raw"))
			stop("raw() data is so far only supported when reading from BLOBs")
		else # TODO: escaping
			statement <- sub("?",paste("'",.mapiQuote(toString(value)),"'",sep=""),statement,fixed=TRUE)
	}
	return(statement)
}

.mapiLongInt <- function(someint) {
	stopifnot(length(someint) == 1)
	formatC(someint,format="d")
}

.hasColFunc <- function(conn,func) {
	tryCatch({
			dbSendQuery(conn,paste0("SELECT ",func,"(1);"))
			TRUE
		}, error = function(e) {
			FALSE
		})
}
