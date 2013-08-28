
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

stop <- function(force=FALSE) {
	gc();
	if(!force && .Call("monetinR_batinUse")) {
		warning("Some results are still referenced by R. If you close the connection, they will be freed and any access to them may crash R.\nCall with force=TRUE to override at your own risks");
		return(FALSE)
	}
	.Call("monetinR_stop");
	gc();
	return(TRUE);
}