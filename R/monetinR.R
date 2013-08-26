
init <- function(dbpath, debugmode = 0) {
	.Call("monetinR_wrapper", dbpath, as.integer(debugmode))
}

dummy <- function() {
	.Call("monetinR_dummy")
}

query <- function(q) {
	.Call("monetinR_executeQuery", q)
}

explain <- function(q) {
	.Call("monetinR_explainQuery", q)
}