
loadFiles <- function() {
  results <- data.frame(tool=character(), 
                        dataset=integer(),
                        op=character(),
                        oparg=numeric(),
                        value=integer())
  
  files <- strsplit(list.files(path="/export/scratch2/lajus/monetdb/tests/benchmark-20130712-144708",
                               pattern="*.RData",
                               full.names=TRUE,
                               recursive=TRUE,
                               include.dirs=TRUE), '/')
  i <- 1
  datatest <- c()
  for (file in files) {
    tool <- file[length(file) -1]
    name <- file[length(file)]
    mdata <- strsplit(sub("n", "", sub(".RData", "", name)), '_')
    e <- new.env(parent=emptyenv())
    load(paste(file, collapse='/'), envir=e)
    bench <- as.list(get("test", envir=e))
    if(!tool == "RSQLite") { print(paste(tool, name))
                             print(bench) }
    datatest[[i]] <- bench
    #print(bench)
    tmp <- data.frame(c(tool),
                      c(as.integer(mdata[[1]][1])),
                      c(mdata[[1]][2]),
                      c(mdata[[1]][3]),
                      c(i))
    colnames(tmp) <- colnames(results)
    results <- rbind(results, tmp)
    i <- i + 1
  }
  #print(i)
  #results$tool <- factor(results$tool, levels=c("monetinR", "MonetDB.R", "RSQLite"), ordered=TRUE)
  res <- c()
  res[[1]] <- results
  res[[2]] <- datatest
  return(res)
}

df <- loadFiles()
res <- df[[1]]

for(l1 in split(res, res$dataset)) {
for(l2 in split(l1, l1$op)) {
for(l3 in split(l2, l2$oparg)) {
  if(length(l3$value) > 1) {
  c <- combn(l3$value, 2)
  for(i in seq(length(c)/2)) {
    if(!identical(df[[2]][[c[1,i]]], df[[2]][[c[2,i]]])) {
      print(paste("Difference in n", l3$dataset, "_", l3$op, "_", l3$oparg, ":"))
      print(l3[l3$value == c[1,i],]$tool, max.levels=0)
      head(df[[2]][[c[1,i]]])
      print(length(df[[2]][[c[1,i]]]))
      print(length(df[[2]][[c[1,i]]][[1]]))
      par(ask=TRUE)
      print(l3[l3$value == c[2,i],]$tool, max.levels=0)
      head(df[[2]][[c[2,i]]])
      print(length(df[[2]][[c[2,i]]]))
      print(length(df[[2]][[c[2,i]]][[1]]))
      par(ask=TRUE)
      
    }
  }
  }
}
}
}

