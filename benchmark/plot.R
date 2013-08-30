library(ggplot2)
library(gridExtra)

loadFiles <- function() {
  results <- data.frame(tool=character(), 
                        dataset=integer(),
                        op=character(),
                        oparg=numeric(),
                        value=integer())
  
  files <- strsplit(list.files(path="/export/scratch2/lajus/monetdb/tests/benchmark-20130816-153528",
                               pattern="*.RData",
                               full.names=TRUE,
                               recursive=TRUE,
                               include.dirs=TRUE), '/')
  for (file in files) {
    tool <- file[length(file) -1]
    name <- file[length(file)]
    mdata <- strsplit(sub("n", "", sub(".RData", "", name)), '_')
    e <- new.env(parent=emptyenv())
    load(paste(file, collapse='/'), envir=e)
    bench <- as.data.frame(get("bench", envir=e))$time
    tmp <- data.frame(c(tool),
                      c(as.integer(mdata[[1]][1])),
                      c(mdata[[1]][2]),
                      c(mdata[[1]][3]),
                      c(bench))
    colnames(tmp) <- colnames(results)
    results <- rbind(results, tmp)
  }
  results$tool <- factor(results$tool, levels=c("monetinR", "MonetDB.R", "data.table", "RSQLite"), ordered=TRUE)
  return(results)
}

df <- loadFiles()
#print(df)
write.table(df, file="bench.csv", sep=",", row.names=FALSE)

ds_names <- list(500000="10Mb", 5000000="100Mb", 50000000="1Gb",
                 500000000="10Gb")

ds_labeller <- function(var, val) {
  return(ds_names[val])
}

plotResult <- function(df, op) {
  mdata <- df[df$op == op,]
  return(
         ggplot(mdata, aes(x=oparg, y=value, color=tool)) +
         geom_boxplot() +
         facet_wrap( ~dataset) +
         scale_y_log10() +
         ylab("Time (ns)") +
         ggtitle(op)
         )
}

pdf("plots.pdf", onefile=TRUE)
grid.arrange(plotResult(df, "sel"))
grid.arrange(plotResult(df, "proj"))
grid.arrange(plotResult(df, "psel"))
grid.arrange(plotResult(df, "group"))
grid.arrange(plotResult(df, "join"))
dev.off()
    
