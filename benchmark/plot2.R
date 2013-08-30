library(ggplot2)
library(sqldf)

df <- read.csv("bench.csv")
df$value <- df$value*1e-9
sa <- sqldf("select tool, dataset, op, oparg, avg(value) as time from df group by tool,dataset,op,oparg")
labds <- function(val) {
  if (val==500000) {
    return("10Mb")
  }
  if (val==5000000) {
    return("100Mb")
  }
  if (val==50000000) {
    return("1Gb")
  }
  if (val==500000000) {
    return("10Gb")
  }
  return(NA)
}
sa$dataset <- factor(sapply(sa$dataset, labds), levels=c("10Mb", "100Mb", "1Gb", "10Gb"), ordered=TRUE)

s <- ggplot(sa[sa$op=="sel",], aes(x=oparg, y=time)) +
#aes(shape = dataset) +
scale_shape(solid=FALSE) +
geom_point(aes(colour = tool),size=3,alpha=0.5) +
geom_line(aes(colour = tool)) +
facet_wrap( ~dataset) +
scale_y_log10(breaks=c(0.1,1,10,60,600,1200)) +
ylab("Time (sec)") +
xlab("Selectivity") +
scale_x_log10(breaks=c(0.01, 0.1, 0.5)) +
ggtitle("Selection")
s

ps <- ggplot(sa[sa$op=="psel",], aes(x=oparg, y=time)) +
#aes(shape = dataset) +
scale_shape(solid=FALSE) +
geom_point(aes(colour = tool),size=3,alpha=0.5) +
geom_line(aes(colour = tool)) +
facet_wrap( ~dataset) +
scale_y_log10(breaks=c(0.1,1,10,60,600,1200)) +
ylab("Time (sec)") +
xlab("Selectivity (the projection divide by 5 the size of the result set)") +
scale_x_log10(breaks=c(0.01, 0.1, 0.5)) +
ggtitle("Projection & Selection")
ps

g <- sa[sa$op=="group", ]
gopargt <- function(val) {
  if (val > 500) { return("10%") } else { return(as.character(val)) }
}
g$oparg <- factor(sapply(g$oparg, gopargt), levels=c("1","5","500","10%"),
                  ordered=TRUE)

gg <- ggplot(g, aes(x=oparg, y=time)) +
#aes(shape = dataset) +
scale_shape(solid=FALSE) +
geom_point(aes(colour = tool),size=3,alpha=0.5) +
geom_line(aes(group=tool, colour = tool)) +
facet_wrap( ~dataset) +
scale_y_log10(breaks=c(0.1,1,10,60,600,1200)) +
ylab("Time (sec)") +
xlab("Group size") +
#scale_x_log10(breaks=c(0.01, 0.1, 0.5)) +
ggtitle("Grouping")
gg

j <- ggplot(sa[sa$op=="join",], aes(x=oparg, y=time)) +
#aes(shape = dataset) +
scale_shape(solid=FALSE) +
geom_point(aes(colour = tool),size=3,alpha=0.5) +
geom_line(aes(colour = tool)) +
facet_wrap( ~dataset) +
scale_y_log10(breaks=c(0.1,1,10,60,600,1200)) +
ylab("Time (sec)") +
xlab("Size of the joined table") +
scale_x_log10(breaks=c(0.01, 0.1), labels=c("1%", "10%")) +
ggtitle("Join")
j

ggsave(file="sel.png", plot=s, width=10, height=10)
ggsave(file="psel.png", plot=ps, width=10, height=10)
ggsave(file="group.png", plot=gg, width=10, height=10)
ggsave(file="join.png", plot=j, width=10, height=10)















