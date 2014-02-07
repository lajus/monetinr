# this wraps a sql database (in particular MonetDB) with a DBI connector 
# to have it appear like a data.frame

# shorthand constructor, also creates connection to db
mirf <- function(database,table,debug=FALSE) {
	#dburl <- paste0("monetdb://",host,":",port,"/",database)	
	con <- dbConnect(MonetinR(), database)
	monet.frame(con,table,debug)
}
