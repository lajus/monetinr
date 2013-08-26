START TRANSACTION;

CREATE TABLE allnewtriples (
	id integer NOT NULL,
	subject integer NOT NULL,
	predicate integer NOT NULL,
	object integer NOT NULL,
	explicit boolean NOT NULL,
	PRIMARY KEY(id),
	CONSTRAINT unique_key UNIQUE(subject, predicate, object)
);
CREATE INDEX allnewtriples_subject_idx ON allnewtriples (subject);
CREATE INDEX allnewtriples_predicate_idx ON allnewtriples (predicate);
CREATE INDEX allnewtriples_object_idx ON allnewtriples (object);

CREATE TABLE "foreign" (
	id integer NOT NULL,
	subject integer NOT NULL,
	predicate integer NOT NULL,
	object integer NOT NULL,
	FOREIGN KEY (id) REFERENCES allnewtriples (id),
	FOREIGN KEY (subject, predicate, object) REFERENCES allnewtriples (subject, predicate, object)
);

CREATE TABLE "triples" (
	"id"        int NOT NULL,
	"subject"   int NOT NULL,
	"predicate" int NOT NULL,
	"object"    int NOT NULL,
	"explicit"  boolean     NOT NULL,
	CONSTRAINT "triples_subject_predicate_object_unique" UNIQUE ("subject", "predicate", "object")
);
CREATE INDEX "triples_object_idx" ON "triples" ("object");
CREATE INDEX "triples_predicate_idx" ON "triples" ("predicate");
CREATE INDEX "triples_predicate_object_idx" ON "triples" ("predicate", "object");
CREATE INDEX "triples_subject_idx" ON "triples" ("subject");
CREATE INDEX "triples_subject_object_idx" ON "triples" ("subject", "object");
CREATE INDEX "triples_subject_predicate_idx" ON "triples" ("subject", "predicate");

COMMIT;
