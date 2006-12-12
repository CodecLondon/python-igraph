/* -*- mode: C -*-  */
/* 
   IGraph library.
   Copyright (C) 2006  Gabor Csardi <csardi@rmki.kfki.hu>
   MTA RMKI, Konkoly-Thege Miklos st. 29-33, Budapest 1121, Hungary
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc.,  51 Franklin Street, Fifth Floor, Boston, MA 
   02110-1301 USA

*/

#include "common.h"
#include "graphobject.h"
#include "vertexseqobject.h"
#include "edgeseqobject.h"
#include "bfsiter.h"
#include "convert.h"
#include "error.h"
#include "memory.h"

PyTypeObject igraphmodule_GraphType;

/** \defgroup python_interface_graph Graph object
 * \ingroup python_interface */

/**
 * \ingroup python_interface_internal
 * \brief Initializes the internal structures in an \c igraph.Graph object's
 * C representation.
 * 
 * This function must be called whenever we create a new Graph object with
 * \c tp_alloc
 */
void igraphmodule_Graph_init_internal(igraphmodule_GraphObject *self) {
  if (!self) return;
  self->vseq = NULL;
  self->eseq = NULL;
  self->destructor = NULL;
  self->weakreflist = NULL;
  self->g.attr = NULL;
}

/**
 * \ingroup python_interface_graph
 * \brief Creates a new igraph object in Python
 * 
 * This function is called whenever a new \c igraph.Graph object is created in
 * Python. An optional \c n parameter can be passed from Python,
 * representing the number of vertices in the graph. If it is omitted,
 * the default value is 1.
 * 
 * <b>Example call from Python:</b>
\verbatim
g = igraph.Graph(5);
\endverbatim
 *
 * In fact, the parameters are processed by \c igraphmodule_Graph_init
 * 
 * \return the new \c igraph.Graph object or NULL if an error occurred.
 * 
 * \sa igraphmodule_Graph_init
 * \sa igraph_empty
 */
PyObject* igraphmodule_Graph_new(PyTypeObject *type, PyObject *args,
				 PyObject *kwds) {
  igraphmodule_GraphObject *self;

  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  /* don't need it, the constructor will do it */
  /*if (self != NULL) {
    igraph_empty(&self->g, 1, 0);
  }*/
  igraphmodule_Graph_init_internal(self);
  
  return (PyObject*)self;
}

/**
 * \ingroup python_interface_graph
 * \brief Clears the graph object's subobject (before deallocation)
 */
int igraphmodule_Graph_clear(igraphmodule_GraphObject *self) {
  PyObject *tmp;
  PyObject_GC_UnTrack(self);
  
  tmp=self->vseq;
  self->vseq=NULL;
  Py_XDECREF(tmp);

  tmp=self->eseq;
  self->eseq=NULL;
  Py_XDECREF(tmp);

  tmp=self->destructor;
  self->destructor=NULL;
  Py_XDECREF(tmp);

  /*
  for (i=0; i<3; i++) {
    tmp=self->attrs[i];
    self->attrs[i]=NULL;
    Py_XDECREF(tmp);
  }
  */
  return 0;
}

/**
 * \ingroup python_interface_graph
 * \brief Support for cyclic garbage collection in Python
 * 
 * This is necessary because the \c igraph.Graph object contains several
 * other \c PyObject pointers and they might point back to itself.
 */
int igraphmodule_Graph_traverse(igraphmodule_GraphObject *self,
				visitproc visit, void *arg) {
  int vret, i;

  RC_TRAVERSE("Graph", self);
  
  if (self->destructor) {
    vret=visit(self->destructor, arg);
    if (vret != 0) return vret;
  }
  
  if (self->g.attr) {
    for (i=0; i<3; i++) {
      vret=visit(((PyObject**)(self->g.attr))[i], arg);
      if (vret != 0) return vret;
    }
  }
  
  // Funny things happen when we traverse the contained VertexSeq or EdgeSeq
  // object (it results in obviously fake memory leaks)
  /*if (self->vseq) {
    vret=visit(self->vseq, arg);
    if (vret != 0) return vret;
  }*/
  
  return 0;
}

/**
 * \ingroup python_interface_graph
 * \brief Deallocates a Python representation of a given igraph object
 */
void igraphmodule_Graph_dealloc(igraphmodule_GraphObject* self) 
{
  PyObject* r;

  // Clear weak references
  if (self->weakreflist != NULL)
    PyObject_ClearWeakRefs((PyObject*)self);
  
  igraph_destroy(&self->g);
  
  if (PyCallable_Check(self->destructor)) {
    r=PyObject_CallObject(self->destructor, NULL);
    if (r) {
      Py_DECREF(r);
    }
  }

  igraphmodule_Graph_clear(self);

  RC_DEALLOC("Graph", self);
  
  PyObject_GC_Del((PyObject*)self);
  // self->ob_type->tp_free((PyObject*)self);
}

/**
 * \ingroup python_interface_graph
 * \brief Initializes a new \c igraph object in Python
 * 
 * This function is called whenever a new \c igraph.Graph object is initialized in
 * Python (note that initializing is not equal to creating: an object might
 * be created but not initialized when it is being recovered from a serialized
 * state).
 * 
 * Throws \c AssertionError in Python if \c vcount is less than or equal to zero.
 * \return the new \c igraph.Graph object or NULL if an error occurred.
 * 
 * \sa igraphmodule_Graph_new
 * \sa igraph_empty
 * \sa igraph_create
 */
int igraphmodule_Graph_init(igraphmodule_GraphObject *self,
			    PyObject *args, PyObject *kwds) {
  char *kwlist[] = {"n", "edges", "directed", NULL};
  int n=1;
  PyObject *edges=NULL, *dir=Py_False;
  igraph_vector_t edges_vector;
   
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|iO!O!", kwlist,
				   &n, &PyList_Type, &edges,
				   &PyBool_Type, &dir))
    return -1;

  if (edges && PyList_Check(edges)) {
    // Caller specified an edge list, so we use igraph_create
    // We have to convert the Python list to a igraph_vector_t
    if (igraphmodule_PyList_to_vector_t(edges, &edges_vector, 1, 1)) {
      igraphmodule_handle_igraph_error();
      return -1;
    }

    /*printf("Edge list:");
    for (i=0; i<n; i++)
      printf(" %d", (int)(VECTOR(edges_vector)[i]));
    printf("\n");*/
    
    if (igraph_create(&self->g, &edges_vector, (igraph_integer_t)n, (dir==Py_True))) {
      igraphmodule_handle_igraph_error();
      return -1;
    }
    
    igraph_vector_destroy(&edges_vector);
  } else {
    // No edge list was specified, let's use igraph_empty
    if (igraph_empty(&self->g, n, (dir==Py_True))) {
      igraphmodule_handle_igraph_error();
      return -1;
    }
  }
   
  return 0;
}

/** \ingroup python_interface_graph
 * \brief Formats an \c igraph.Graph object in a human-readable format.
 * 
 * This function is rather simple now, it returns the number of vertices
 * and edges in a string.
 * 
 * \return the formatted textual representation as a \c PyObject
 */
PyObject* igraphmodule_Graph_str(igraphmodule_GraphObject *self)
{
   if (igraph_is_directed(&self->g))
     return PyString_FromFormat("Directed graph (|V| = %ld, |E| = %ld)",
				(long)igraph_vcount(&self->g),
				(long)igraph_ecount(&self->g));
   else
     return PyString_FromFormat("Undirected graph (|V| = %ld, |E| = %ld)",
				(long)igraph_vcount(&self->g),
				(long)igraph_ecount(&self->g));
}

/** \ingroup python_interface_graph
 * \brief Returns the number of vertices in an \c igraph.Graph object.
 * \return the number of vertices as a \c PyObject
 * \sa igraph_vcount
 */
PyObject* igraphmodule_Graph_vcount(igraphmodule_GraphObject *self) 
{
   PyObject *result;
   result=Py_BuildValue("l", (long)igraph_vcount(&self->g));
   return result;
}

/** \ingroup python_interface_graph
 * \brief Returns the number of edges in an \c igraph.Graph object.
 * \return the number of edges as a \c PyObject
 * \sa igraph_ecount
 */
PyObject* igraphmodule_Graph_ecount(igraphmodule_GraphObject *self) 
{
   PyObject *result;
   result=Py_BuildValue("l", (long)igraph_ecount(&self->g));
   return result;
}

/** \ingroup python_interface_graph
 * \brief Checks whether an \c igraph.Graph object is directed.
 * \return \c True if the graph is directed, \c False otherwise.
 * \sa igraph_is_directed
 */
PyObject* igraphmodule_Graph_is_directed(igraphmodule_GraphObject *self) 
{
   if (igraph_is_directed(&self->g)) {
     Py_INCREF(Py_True); return Py_True;
   } else {
     Py_INCREF(Py_False); return Py_False;
   }
}

/** \ingroup python_interface_graph
 * \brief Adds vertices to an \c igraph.Graph
 * \return the extended \c igraph.Graph object
 * \sa igraph_add_vertices
 */
PyObject* igraphmodule_Graph_add_vertices(igraphmodule_GraphObject *self,
						 PyObject *args,
						 PyObject *kwds) 
{
   long n;
   
   if (!PyArg_ParseTuple(args, "l", &n)) return NULL;
   if (n<0)
     {
	// Throw an exception
	PyErr_SetString(PyExc_AssertionError, "Number of vertices to be added can't be negative.");
	return NULL;
     }
   if (igraph_add_vertices(&self->g, (igraph_integer_t)n, 0)) {
      igraphmodule_handle_igraph_error();
      return NULL;
   }
   
   Py_INCREF(self);
   
   return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Removes vertices from an \c igraph.Graph
 * \return the modified \c igraph.Graph object
 * 
 * \todo Need more error checking on vertex IDs. (igraph fails when an
 * invalid vertex ID is given)
 * \sa igraph_delete_vertices
 */
PyObject* igraphmodule_Graph_delete_vertices(igraphmodule_GraphObject *self,
						    PyObject *args,
						    PyObject *kwds)
{
   PyObject *list;
   igraph_vector_t v;
   
   if (!PyArg_ParseTuple(args, "O", &list)) return NULL;
   if (igraphmodule_PyList_to_vector_t(list, &v, 1, 0))
     {
	// something bad happened during conversion
	return NULL;
     }
   
   // do the hard work :)
   if (igraph_delete_vertices(&self->g, igraph_vss_vector(&v)))
     {
	igraphmodule_handle_igraph_error();
	igraph_vector_destroy(&v);
	return NULL;
     }

   igraph_vector_destroy(&v);
   
   Py_INCREF(self);
   
   return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Adds edges to an \c igraph.Graph
 * \return the extended \c igraph.Graph object
 * 
 * \todo Need more error checking on vertex IDs. (igraph fails when an
 * invalid vertex ID is given)
 * \sa igraph_add_edges
 */
PyObject* igraphmodule_Graph_add_edges(igraphmodule_GraphObject *self,
					      PyObject *args,
					      PyObject *kwds) 
{
   PyObject *list;
   igraph_vector_t v;

   if (!PyArg_ParseTuple(args, "O", &list)) return NULL;
   Py_INCREF(list);
   
   if (igraphmodule_PyList_to_vector_t(list, &v, 1, 1))
     {
	// something bad happened during conversion, release the
	// list reference and return immediately
	Py_DECREF(list);
	return NULL;
     }
   
   // do the hard work :)
   if (igraph_add_edges(&self->g, &v, 0)) 
     {
	igraphmodule_handle_igraph_error();
	Py_DECREF(list);
	igraph_vector_destroy(&v);
	return NULL;
     }
   
   Py_DECREF(list);
   
   Py_INCREF(self);
   
   igraph_vector_destroy(&v);
   
   return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Deletes edges from an \c igraph.Graph
 * \return the extended \c igraph.Graph object
 * 
 * \todo Need more error checking on vertex IDs. (igraph fails when an
 * invalid vertex ID is given)
 * \sa igraph_delete_edges
 */
PyObject* igraphmodule_Graph_delete_edges(igraphmodule_GraphObject *self,
					  PyObject *args,
					  PyObject *kwds) {
  PyObject *list, *by_index=Py_False;
  igraph_vector_t v;
  igraph_es_t es;
  static char* kwlist[] = {"edges", "by_index", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O", kwlist,
				   &list, &by_index)) return NULL;
  
  if (PyObject_IsTrue(by_index)) {
    if (igraphmodule_PyList_to_vector_t(list, &v, 1, 0)) {
      /* something bad happened during conversion, return immediately */
      return NULL;
    }
   
    /* do the hard work :) */
    if (igraph_es_vector(&es, &v)) {
      igraphmodule_handle_igraph_error();
      igraph_vector_destroy(&v);
      return NULL;
    }
  } else {
    if (igraphmodule_PyList_to_vector_t(list, &v, 1, 1)) {
      /* something bad happened during conversion, return immediately */
      return NULL;
    }
   
    /* do the hard work :) */
    if (igraph_es_pairs(&es, &v, IGRAPH_DIRECTED)) {
      igraphmodule_handle_igraph_error();
      igraph_vector_destroy(&v);
      return NULL;
    }
  }

  if (igraph_delete_edges(&self->g, es)) {
    igraphmodule_handle_igraph_error();
    igraph_es_destroy(&es);
    igraph_vector_destroy(&v);
    return NULL;
  }
   
  Py_INCREF(self);
   
  igraph_es_destroy(&es);
  igraph_vector_destroy(&v);
   
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief The degree of some vertices in an \c igraph.Graph
 * \return the degree list as a Python object
 * \sa igraph_degree
 */
PyObject* igraphmodule_Graph_degree(igraphmodule_GraphObject *self,
					   PyObject *args,
					   PyObject *kwds) 
{
   PyObject *list=Py_None;
   int dtype=IGRAPH_ALL;
   PyObject *loops = Py_False;
   igraph_vector_t result;
   igraph_vs_t vs;
   igraph_bool_t return_single=0;
   
   static char *kwlist[] = { "vertices", "type", "loops", NULL };

   if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OiO", kwlist,
				    &list, &dtype, &loops))
	return NULL;
   
   if (dtype!=IGRAPH_ALL && dtype!=IGRAPH_OUT && dtype!=IGRAPH_IN) 
     {
	PyErr_SetString(PyExc_ValueError, "dtype should be either ALL or IN or OUT");
	return NULL;
     }

   if (igraphmodule_PyObject_to_vs_t(list, &vs, &return_single)) 
     {
	igraphmodule_handle_igraph_error();
	return NULL;
     }
   
   if (igraph_vector_init(&result, 0)) 
     {
	igraph_vs_destroy(&vs);
	return NULL;
     }
   
   if (igraph_degree(&self->g, &result, vs,
		     (igraph_neimode_t)dtype, PyObject_IsTrue(loops)))
     {
       igraphmodule_handle_igraph_error();
       igraph_vs_destroy(&vs);
       igraph_vector_destroy(&result);
       return NULL;
     }
   
   if (!return_single)
     list=igraphmodule_vector_t_to_PyList(&result);
   else
     list=PyInt_FromLong(VECTOR(result)[0]);
   
   igraph_vector_destroy(&result);
   igraph_vs_destroy(&vs);
   
   return list;
}

/** \ingroup python_interface_graph
 * \brief The maximum degree of some vertices in an \c igraph.Graph
 * \return the maxium degree as a Python object
 * \sa igraph_maxdegree
 */
PyObject* igraphmodule_Graph_maxdegree(igraphmodule_GraphObject *self,
				       PyObject *args,
				       PyObject *kwds) 
{
   PyObject *list=Py_None;
   int dtype=IGRAPH_ALL;
   PyObject *loops = Py_False;
   igraph_integer_t result;
   igraph_vs_t vs;
   igraph_bool_t return_single=0;
   
   static char *kwlist[] = { "vertices", "type", "loops", NULL };

   if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OiO", kwlist,
				    &list, &dtype, &loops))
	return NULL;
   
   if (dtype!=IGRAPH_ALL && dtype!=IGRAPH_OUT && dtype!=IGRAPH_IN) 
     {
	PyErr_SetString(PyExc_ValueError, "dtype should be either ALL or IN or OUT");
	return NULL;
     }

   if (igraphmodule_PyObject_to_vs_t(list, &vs, &return_single)) 
     {
	igraphmodule_handle_igraph_error();
	return NULL;
     }
   
   if (igraph_maxdegree(&self->g, &result, vs,
			(igraph_neimode_t)dtype, PyObject_IsTrue(loops)))
     {
       igraphmodule_handle_igraph_error();
       igraph_vs_destroy(&vs);
       return NULL;
     }
   
   igraph_vs_destroy(&vs);
   
   return PyInt_FromLong((long)result);
}

/** \ingroup python_interface_graph
 * \brief The neighbors of a given vertex in an \c igraph.Graph
 * This method accepts a single vertex ID as a parameter, and returns the
 * neighbors of the given vertex in the form of an integer list. A
 * second argument may be passed as well, meaning the type of neighbors to
 * be returned (\c OUT for successors, \c IN for predecessors or \c ALL
 * for both of them). This argument is ignored for undirected graphs.
 * 
 * \return the neighbor list as a Python list object
 * \sa igraph_neighbors
 */
PyObject* igraphmodule_Graph_neighbors(igraphmodule_GraphObject *self,
					      PyObject *args,
					      PyObject *kwds) 
{
   PyObject *list;
   int dtype=IGRAPH_ALL;
   long idx;
   igraph_vector_t result;
   
   char *kwlist[] = 
     {
	"vertex", "type", NULL
     }
   ;

   if (!PyArg_ParseTupleAndKeywords(args, kwds, "l|i", kwlist,
				    &idx, &dtype))
     return NULL;
   
   if (dtype!=IGRAPH_ALL && dtype!=IGRAPH_OUT && dtype!=IGRAPH_IN) 
     {
	PyErr_SetString(PyExc_ValueError, "type should be either ALL or IN or OUT");
	return NULL;
     }
   
   igraph_vector_init(&result, 1);
   if (igraph_neighbors(&self->g, &result, idx, (igraph_neimode_t)dtype))
     {
	igraphmodule_handle_igraph_error();
	igraph_vector_destroy(&result);
	return NULL;
     }
   
   list=igraphmodule_vector_t_to_PyList(&result);
   igraph_vector_destroy(&result);
   
   return list;
}

/** \ingroup python_interface_graph
 * \brief The successors of a given vertex in an \c igraph.Graph
 * This method accepts a single vertex ID as a parameter, and returns the
 * successors of the given vertex in the form of an integer list. It
 * is equivalent to calling \c igraph.Graph.neighbors with \c type=OUT
 * 
 * \return the successor list as a Python list object
 * \sa igraph_neighbors
 */
PyObject* igraphmodule_Graph_successors(igraphmodule_GraphObject *self,
					       PyObject *args,
					       PyObject *kwds) 
{
  PyObject *list;
  long idx;
  igraph_vector_t result;
   
  char *kwlist[] = {"vertex", NULL};
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "l", kwlist, &idx))
    return NULL;
   
  igraph_vector_init(&result, 1);
  if (igraph_neighbors(&self->g, &result, idx, IGRAPH_OUT))  {
    igraphmodule_handle_igraph_error();
    igraph_vector_destroy(&result);
    return NULL;
  }
   
   list=igraphmodule_vector_t_to_PyList(&result);
   igraph_vector_destroy(&result);
   
   return list;
}

/** \ingroup python_interface_graph
 * \brief The predecessors of a given vertex in an \c igraph.Graph
 * This method accepts a single vertex ID as a parameter, and returns the
 * predecessors of the given vertex in the form of an integer list. It
 * is equivalent to calling \c igraph.Graph.neighbors with \c type=IN
 * 
 * \return the predecessor list as a Python list object
 * \sa igraph_neighbors
 */
PyObject* igraphmodule_Graph_predecessors(igraphmodule_GraphObject *self,
						 PyObject *args,
						 PyObject *kwds) 
{
  PyObject *list;
  long idx;
  igraph_vector_t result;
   
  char *kwlist[] = {"vertex", NULL};
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "l", kwlist, &idx))
    return NULL;
   
  igraph_vector_init(&result, 1);
  if (igraph_neighbors(&self->g, &result, idx, IGRAPH_IN))  {
    igraphmodule_handle_igraph_error();
    igraph_vector_destroy(&result);
    return NULL;
  }
   
   list=igraphmodule_vector_t_to_PyList(&result);
   igraph_vector_destroy(&result);
   
   return list;
}

/** \ingroup python_interface_graph
 * \brief Returns the ID of an arbitrary edge between the given two nodes
 * \sa igraph_get_eid
 */
PyObject* igraphmodule_Graph_get_eid(igraphmodule_GraphObject *self,
				     PyObject *args, PyObject *kwds) {
  static char* kwlist[] = { "v1", "v2", "directed", NULL };
  long v1, v2;
  igraph_integer_t result;
  PyObject *directed = Py_False;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii|O", kwlist, &v1, &v2,
				   &directed))
    return NULL;
  if (igraph_get_eid(&self->g, &result, v1, v2, PyObject_IsTrue(directed)))
    return igraphmodule_handle_igraph_error();

  return Py_BuildValue("i", (long)result);
}

/** \ingroup python_interface_graph
 * \brief Calculates the diameter of an \c igraph.Graph
 * This method accepts two optional parameters: the first one is
 * a boolean meaning whether to consider directed paths (and is
 * ignored for undirected graphs). The second one is only meaningful
 * in unconnected graphs: it is \c True if the longest geodesic
 * within a component should be returned and \c False if the number of
 * vertices should be returned. They both have a default value of \c False.
 * 
 * \return the diameter as a Python integer
 * \sa igraph_diameter
 */
PyObject* igraphmodule_Graph_diameter(igraphmodule_GraphObject *self,
					     PyObject *args,
					     PyObject *kwds) 
{
  PyObject *dir=NULL, *vcount_if_unconnected=NULL;
  igraph_integer_t i;
  int r;
   
  char *kwlist[] = {
    "directed", "unconn", NULL
  };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O!O!", kwlist,
				   &PyBool_Type, &dir,
				   &PyBool_Type, &vcount_if_unconnected))
    return NULL;
  
  r=igraph_diameter(&self->g, &i, 0, 0, 0, (igraph_bool_t)(dir == Py_True),
		    (igraph_bool_t)(vcount_if_unconnected == Py_True));
  if (r) 
     {
	igraphmodule_handle_igraph_error();
	return NULL;
     }
   
   return PyInt_FromLong((long)i);
}

/** \ingroup python_interface_graph
 * \brief Generates a graph from its adjacency matrix
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_adjacency
 */
PyObject* igraphmodule_Graph_Adjacency(PyTypeObject *type,
				       PyObject *args,
				       PyObject *kwds) {
  igraphmodule_GraphObject *self;
  igraph_matrix_t m;
  PyObject *matrix;
  igraph_adjacency_t mode = IGRAPH_ADJ_DIRECTED;
  
  char *kwlist[] = {"matrix", "mode", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!|i", kwlist,
				   &PyList_Type, &matrix, &mode))
    return NULL;
      
  if (igraphmodule_PyList_to_matrix_t(matrix, &m)) {
    PyErr_SetString(PyExc_TypeError, "Error while converting adjacency matrix");
    return NULL;
  }
  
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (igraph_adjacency(&self->g, &m, mode)) {
      igraphmodule_handle_igraph_error();
      igraph_matrix_destroy(&m);
      return NULL;
    }
  }
   
  igraph_matrix_destroy(&m);
  
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a graph from the Graph Atlas
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_atlas
 */
PyObject* igraphmodule_Graph_Atlas(PyTypeObject *type,
				   PyObject *args) {
  long n;
  igraphmodule_GraphObject *self;
  
  if (!PyArg_ParseTuple(args, "l", &n)) return NULL;
  
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (igraph_atlas(&self->g, (igraph_integer_t)n)) {
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  }
   
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a graph based on the Barabasi-Albert model
 * This is intended to be a class method in Python, so the first argument
 * is the type object and not the Python igraph object (because we have
 * to allocate that in this method).
 * 
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_barabasi_game
 */
PyObject* igraphmodule_Graph_Barabasi(PyTypeObject *type,
				      PyObject *args,
				      PyObject *kwds) 
{
  igraphmodule_GraphObject *self;
  long n, m=0;
  float power=0.0, zero_appeal=0.0;
  igraph_vector_t outseq;
  PyObject *m_obj, *outpref=Py_False, *directed=Py_False;
  
  char *kwlist[] = {"n", "m", "outpref", "directed", "power", "zero_appeal", NULL};
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "lO|OOff", kwlist,
				   &n, &m_obj, &outpref, &directed, &power, &zero_appeal))
    return NULL;
  
  if (n<0) {
    PyErr_SetString(PyExc_ValueError, "Number of vertices must be positive.");
    return NULL;
  }
  
  // let's check whether we have a constant out-degree or a list
  if (PyInt_Check(m_obj)) {
    m=PyInt_AsLong(m_obj);
    igraph_vector_init(&outseq, 0);
  } else if (PyList_Check(m_obj)) {
    if (igraphmodule_PyList_to_vector_t(m_obj, &outseq, 1, 0)) {
      // something bad happened during conversion
      return NULL;
    }
  }
  
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (power == 0.0) {
      /* linear model */
      if (igraph_barabasi_game(&self->g, (igraph_integer_t)n,
			       (igraph_integer_t)m,
			       &outseq, PyObject_IsTrue(outpref),
			       PyObject_IsTrue(directed))) {
	igraphmodule_handle_igraph_error();
	igraph_vector_destroy(&outseq);
	return NULL;
      }
    } else {
      /* nonlinear model */
      if (igraph_nonlinear_barabasi_game(&self->g, (igraph_integer_t)n,
					 (igraph_real_t)power,
					 (igraph_integer_t)m,
					 &outseq, PyObject_IsTrue(outpref),
					 (igraph_real_t)zero_appeal,
					 PyObject_IsTrue(directed))) {
	igraphmodule_handle_igraph_error();
	igraph_vector_destroy(&outseq);
	return NULL;
      }
    }
  }
  
  igraph_vector_destroy(&outseq);
  
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a graph based on the Erd�s-R�nyi model
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_erdos_renyi_game
 */
PyObject* igraphmodule_Graph_Erdos_Renyi(PyTypeObject *type,
						PyObject *args,
						PyObject *kwds) 
{
  igraphmodule_GraphObject *self;
  long n, m=-1;
  double p=-1.0;
  igraph_erdos_renyi_t t;
  PyObject *loops=NULL, *directed=NULL;
  
  char *kwlist[] = {"n", "p", "m", "directed", "loops", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "l|dlO!O!", kwlist,
				   &n, &p, &m,
				   &PyBool_Type, &directed,
				   &PyBool_Type, &loops))
    return NULL;
      
  if (n<0) {
    PyErr_SetString(PyExc_ValueError, "Number of vertices must be positive.");
    return NULL;
  }
   
  if (m==-1 && p==-1.0) {
    // no density parameters were given, throw exception
    PyErr_SetString(PyExc_TypeError, "Either m or p must be given.");
    return NULL;
  }
  if (m!=-1 && p!=-1.0) {
    // both density parameters were given, throw exception
    PyErr_SetString(PyExc_TypeError, "Only one must be given from m and p.");
    return NULL;
  }
   
  t=(m==-1)?IGRAPH_ERDOS_RENYI_GNP:IGRAPH_ERDOS_RENYI_GNM;
   
  if (t==IGRAPH_ERDOS_RENYI_GNP) {
    if (p<0.0 || p>1.0) {
      // Invalid probability was given, throw exception
      PyErr_SetString(PyExc_ValueError, "p must be between 0 and 1.");
      return NULL;
    }	
  } else {
    if (m<0 || m>n*n) {
      // Invalid edge count was given, throw exception
      PyErr_SetString(PyExc_ValueError, "m must be between 0 and n^2.");
      return NULL;
    }	
  }
      
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (igraph_erdos_renyi_game(&self->g, t, (igraph_integer_t)n,
				(igraph_real_t)((t==IGRAPH_ERDOS_RENYI_GNM)?m:p),
				(directed == Py_True),
				(loops == Py_True))) {
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  }
   
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a graph based on a simple growing model with vertex types
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_establishment_game
 */
PyObject* igraphmodule_Graph_Establishment(PyTypeObject *type,
					   PyObject *args,
					   PyObject *kwds) 
{
  igraphmodule_GraphObject *self;
  long n, types, k;
  PyObject *type_dist, *pref_matrix;
  PyObject *directed = Py_False;
  igraph_matrix_t pm;
  igraph_vector_t td;
  
  char *kwlist[] = {"n", "k", "type_dist", "pref_matrix", "directed", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "llO!O!|O", kwlist,
				   &n, &k, &PyList_Type, &type_dist,
				   &PyList_Type, &pref_matrix,
				   &directed))
    return NULL;
      
  if (n<=0 || k<=0) {
    PyErr_SetString(PyExc_ValueError, "Number of vertices and the amount of connection trials per step must be positive.");
    return NULL;
  }
  types = PyList_Size(type_dist);
  
  if (igraphmodule_PyList_to_matrix_t(pref_matrix, &pm)) {
    PyErr_SetString(PyExc_TypeError, "Error while converting preference matrix");
    return NULL;
  }
  if (igraph_matrix_nrow(&pm) != igraph_matrix_ncol(&pm) ||
      igraph_matrix_nrow(&pm) != types) {
    PyErr_SetString(PyExc_ValueError, "Preference matrix must have exactly the same rows and columns as the number of types");
    igraph_matrix_destroy(&pm);
    return NULL;
  }
  if (igraphmodule_PyList_to_vector_t(type_dist, &td, 1, 0)) {
    PyErr_SetString(PyExc_ValueError, "Error while converting type distribution vector");
    igraph_matrix_destroy(&pm);
    return NULL;
  }
  
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (igraph_establishment_game(&self->g, (igraph_integer_t)n,
				  (igraph_integer_t)types,
				  (igraph_integer_t)k, &td, &pm,
				  PyObject_IsTrue(directed))) {
      igraphmodule_handle_igraph_error();
      igraph_matrix_destroy(&pm);
      igraph_vector_destroy(&td);
      return NULL;
    }
  }
   
  igraph_matrix_destroy(&pm);
  igraph_vector_destroy(&td);
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a full graph
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_full
 */
PyObject* igraphmodule_Graph_Full(PyTypeObject *type,
					 PyObject *args,
					 PyObject *kwds) 
{
  igraphmodule_GraphObject *self;
  long n;
  PyObject *loops=NULL, *directed=NULL;
  
  char *kwlist[] = {"n", "directed", "loops", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "l|O!O!", kwlist, &n,
				   &PyBool_Type, &directed,
				   &PyBool_Type, &loops))
    return NULL;
  
  if (n<0) {
    PyErr_SetString(PyExc_ValueError, "Number of vertices must be positive.");
    return NULL;
  }
   
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (igraph_full(&self->g, (igraph_integer_t)n,
		    (directed == Py_True), (loops == Py_True))) {
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  }
   
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a graph based on the geometric random model
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_grg_game
 */
PyObject* igraphmodule_Graph_GRG(PyTypeObject *type,
				 PyObject *args,
				 PyObject *kwds) {
  igraphmodule_GraphObject *self;
  long n;
  double r;
  PyObject *torus=Py_False;
  
  char *kwlist[] = {"n", "radius", "torus", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "ld|O", kwlist,
				   &n, &r, &torus))
    return NULL;
      
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (igraph_grg_game(&self->g, (igraph_integer_t)n, (igraph_real_t)r,
			PyObject_IsTrue(torus))) {
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  }
   
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a growing random graph
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_growing_random_game
 */
PyObject* igraphmodule_Graph_Growing_Random(PyTypeObject *type,
						   PyObject *args,
						   PyObject *kwds) 
{
   long n, m;
   PyObject *directed=NULL, *citation=NULL;
   igraphmodule_GraphObject *self;
   
   char *kwlist[] = {"n", "m", "directed", "citation", NULL};

   if (!PyArg_ParseTupleAndKeywords(args, kwds, "ll|O!O!", kwlist, &n, &m,
				    &PyBool_Type, &directed,
				    &PyBool_Type, &citation))
    return NULL;
  
  if (n<0) {
    PyErr_SetString(PyExc_ValueError, "Number of vertices must be positive.");
    return NULL;
  }
  
  if (m<0) {
    PyErr_SetString(PyExc_ValueError, "Number of new edges per iteration must be positive.");
    return NULL;
  }
   
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (igraph_growing_random_game(&self->g, (igraph_integer_t)n,
				   (igraph_integer_t)m, (directed == Py_True),
				   (citation == Py_True))) {
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  }
   
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a star graph
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_star
 */
PyObject* igraphmodule_Graph_Star(PyTypeObject *type,
					 PyObject *args,
					 PyObject *kwds) {
  long n, center=0;
  igraph_star_mode_t mode=IGRAPH_STAR_UNDIRECTED;
  igraphmodule_GraphObject *self;
  
  char *kwlist[] = {"n", "mode", "center", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "l|ll", kwlist,
				   &n, &mode, &center))
    return NULL;
  
  if (n<0) {
    PyErr_SetString(PyExc_ValueError, "Number of vertices must be positive.");
    return NULL;
  }
   
  if (center>=n || center<0) {
    PyErr_SetString(PyExc_ValueError, "Central vertex ID should be between 0 and n-1");
    return NULL;
  }
   
  if (mode!=IGRAPH_STAR_UNDIRECTED && mode!=IGRAPH_STAR_IN &&
      mode!=IGRAPH_STAR_OUT) {
    PyErr_SetString(PyExc_ValueError, "Mode should be either STAR_IN, STAR_OUT or STAR_UNDIRECTED.");
    return NULL;
  }
  
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (igraph_star(&self->g, (igraph_integer_t)n, mode, (igraph_integer_t)center)) {
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  }
   
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a regular lattice
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_lattice
 */
PyObject* igraphmodule_Graph_Lattice(PyTypeObject *type,
				     PyObject *args,
				     PyObject *kwds) {
  igraph_vector_t dimvector;
  long nei=1, ndims, i;
  igraph_bool_t directed;
  igraph_bool_t mutual;
  igraph_bool_t circular;
  PyObject *o_directed=Py_False, *o_mutual=Py_True, *o_circular=Py_True;
  PyObject *o_dimvector=Py_None, *o;
  igraphmodule_GraphObject *self;
  
  char *kwlist[] = {"dim", "nei", "directed", "mutual", "circular", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!|lOOO", kwlist,
				   &PyList_Type, &o_dimvector,
				   &nei, &o_directed, &o_mutual, &o_circular))
    return NULL;

  directed=PyObject_IsTrue(o_directed);
  mutual=PyObject_IsTrue(o_mutual);
  circular=PyObject_IsTrue(o_circular);

  ndims=PyList_Size(o_dimvector);
  igraph_vector_init(&dimvector, ndims);
  for (i=0; i<ndims; i++) {
    o=PyList_GetItem(o_dimvector, i);
    if (o) {
      if (PyInt_Check(o))
	VECTOR(dimvector)[i] = (igraph_integer_t)PyInt_AsLong(o);
      else {
	PyErr_SetString(PyExc_TypeError, "Dimension list must contain integers");
	igraph_vector_destroy(&dimvector);
	return NULL;
      }
    } else {
      igraph_vector_destroy(&dimvector);
      return NULL;
    }
  }
	
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (igraph_lattice(&self->g, &dimvector, nei, directed, mutual, circular)) {
      igraph_vector_destroy(&dimvector);
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  }
   
  igraph_vector_destroy(&dimvector);
  
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a graph based on vertex types and connection preferences
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_preference_game
 */
PyObject* igraphmodule_Graph_Preference(PyTypeObject *type,
					PyObject *args,
					PyObject *kwds) {
  igraphmodule_GraphObject *self;
  long n, types;
  PyObject *type_dist, *pref_matrix;
  PyObject *directed = Py_False;
  PyObject *loops = Py_False;
  igraph_matrix_t pm;
  igraph_vector_t td;
  igraph_vector_t type_vec;
  PyObject *type_vec_o;
  PyObject *attribute_key=Py_None;
  igraph_bool_t store_attribs;

  char *kwlist[] = {"n", "type_dist", "pref_matrix", "attribute", "directed", "loops", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "lO!O!|OOO", kwlist,
				   &n, &PyList_Type, &type_dist,
				   &PyList_Type, &pref_matrix,
				   &attribute_key, &directed, &loops))
    return NULL;
      
  if (n<=0) {
    PyErr_SetString(PyExc_ValueError, "Number of vertices must be positive.");
    return NULL;
  }
  types = PyList_Size(type_dist);
  
  if (igraphmodule_PyList_to_matrix_t(pref_matrix, &pm)) {
    PyErr_SetString(PyExc_TypeError, "Error while converting preference matrix");
    return NULL;
  }

  if (igraph_matrix_nrow(&pm) != igraph_matrix_ncol(&pm) ||
      igraph_matrix_nrow(&pm) != types) {
    PyErr_SetString(PyExc_ValueError, "Preference matrix must have exactly the same rows and columns as the number of types");
    igraph_matrix_destroy(&pm);
    return NULL;
  }
  if (igraphmodule_PyList_to_vector_t(type_dist, &td, 1, 0)) {
    PyErr_SetString(PyExc_ValueError, "Error while converting type distribution vector");
    igraph_matrix_destroy(&pm);
    return NULL;
  }
  
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    store_attribs = (attribute_key && attribute_key != Py_None);
    if (store_attribs && igraph_vector_init(&type_vec, (igraph_integer_t)n)) {
      igraph_matrix_destroy(&pm);
      igraph_vector_destroy(&td);
      igraphmodule_handle_igraph_error();
      return NULL;
    }

    igraphmodule_Graph_init_internal(self);
    if (igraph_preference_game(&self->g, (igraph_integer_t)n,
			       (igraph_integer_t)types, &td, &pm,
			       store_attribs ? &type_vec : 0,
			       PyObject_IsTrue(directed),
			       PyObject_IsTrue(loops))) {
      igraphmodule_handle_igraph_error();
      igraph_matrix_destroy(&pm);
      igraph_vector_destroy(&td);
      if (store_attribs) igraph_vector_destroy(&type_vec);
      return NULL;
    }

    if (store_attribs) {
      type_vec_o=igraphmodule_vector_t_to_PyList(&type_vec);
      if (type_vec_o == 0) {
	igraph_matrix_destroy(&pm);
	igraph_vector_destroy(&td);
	igraph_vector_destroy(&type_vec);
	Py_DECREF(self);
	return NULL;
      }
      if (attribute_key != Py_None && attribute_key != 0) {
	if (PyDict_SetItem(((PyObject**)self->g.attr)[ATTRHASH_IDX_VERTEX],
			   attribute_key, type_vec_o) == -1) {
	  Py_DECREF(type_vec_o);
	  igraph_matrix_destroy(&pm);
	  igraph_vector_destroy(&td);
	  igraph_vector_destroy(&type_vec);
	  Py_DECREF(self);
	  return NULL;
	}
      }

      Py_DECREF(type_vec_o);
      igraph_vector_destroy(&type_vec);
    }
  }
   
  igraph_matrix_destroy(&pm);
  igraph_vector_destroy(&td);
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a graph based on asymmetric vertex types and connection preferences
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_asymmetric_preference_game
 */
PyObject* igraphmodule_Graph_Asymmetric_Preference(PyTypeObject *type,
						   PyObject *args,
						   PyObject *kwds) {
  igraphmodule_GraphObject *self;
  long n, types;
  PyObject *type_dist_matrix, *pref_matrix;
  PyObject *loops = Py_False;
  igraph_matrix_t pm;
  igraph_matrix_t td;
  igraph_vector_t in_type_vec, out_type_vec;
  PyObject *type_vec_o;
  PyObject *attribute_key=Py_None;
  igraph_bool_t store_attribs;
  
  char *kwlist[] = {"n", "type_dist_matrix", "pref_matrix", "attribute", "loops", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "lO!O!|OO", kwlist,
				   &n, &PyList_Type, &type_dist_matrix,
				   &PyList_Type, &pref_matrix,
				   &attribute_key, &loops))
    return NULL;
      
  if (n<=0) {
    PyErr_SetString(PyExc_ValueError, "Number of vertices must be positive.");
    return NULL;
  }
  types = PyList_Size(type_dist_matrix);
  
  if (igraphmodule_PyList_to_matrix_t(pref_matrix, &pm)) {
    PyErr_SetString(PyExc_TypeError, "Error while converting preference matrix");
    return NULL;
  }

  if (igraph_matrix_nrow(&pm) != igraph_matrix_ncol(&pm) ||
      igraph_matrix_nrow(&pm) != types) {
    PyErr_SetString(PyExc_ValueError, "Preference matrix must have exactly the same rows and columns as the number of types");
    igraph_matrix_destroy(&pm);
    return NULL;
  }
  if (igraphmodule_PyList_to_matrix_t(type_dist_matrix, &td)) {
    PyErr_SetString(PyExc_ValueError, "Error while converting type distribution matrix");
    igraph_matrix_destroy(&pm);
    return NULL;
  }
  
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    store_attribs = (attribute_key && attribute_key != Py_None);
    if (store_attribs) {
      if (igraph_vector_init(&in_type_vec, (igraph_integer_t)n)) {
	igraph_matrix_destroy(&pm);
	igraph_matrix_destroy(&td);
	igraphmodule_handle_igraph_error();
	return NULL;
      }
      if (igraph_vector_init(&out_type_vec, (igraph_integer_t)n)) {
	igraph_matrix_destroy(&pm);
	igraph_matrix_destroy(&td);
	igraph_vector_destroy(&in_type_vec);
	igraphmodule_handle_igraph_error();
	return NULL;
      }
    }

    igraphmodule_Graph_init_internal(self);
    if (igraph_asymmetric_preference_game(&self->g, (igraph_integer_t)n,
					  (igraph_integer_t)types, &td, &pm,
					  store_attribs ? &in_type_vec : 0,
					  store_attribs ? &out_type_vec : 0,
					  PyObject_IsTrue(loops))) {
      igraphmodule_handle_igraph_error();
      igraph_vector_destroy(&in_type_vec);
      igraph_vector_destroy(&out_type_vec);
      igraph_matrix_destroy(&pm);
      igraph_matrix_destroy(&td);
      return NULL;
    }

    if (store_attribs) {
      type_vec_o=igraphmodule_vector_t_pair_to_PyList(&in_type_vec,
						      &out_type_vec);
      if (type_vec_o == NULL) {
	igraph_matrix_destroy(&pm);
	igraph_vector_destroy(&td);
	igraph_vector_destroy(&in_type_vec);
	igraph_vector_destroy(&out_type_vec);
	Py_DECREF(self);
	return NULL;
      }
      if (attribute_key != Py_None && attribute_key != 0) {
	if (PyDict_SetItem(((PyObject**)self->g.attr)[ATTRHASH_IDX_VERTEX],
			   attribute_key, type_vec_o) == -1) {
	  Py_DECREF(type_vec_o);
	  igraph_matrix_destroy(&pm);
	  igraph_vector_destroy(&td);
	  igraph_vector_destroy(&in_type_vec);
	  igraph_vector_destroy(&out_type_vec);
	  Py_DECREF(self);
	  return NULL;
	}
      }

      Py_DECREF(type_vec_o);
      igraph_vector_destroy(&in_type_vec);
      igraph_vector_destroy(&out_type_vec);
    }
  }
   
  igraph_matrix_destroy(&pm);
  igraph_matrix_destroy(&td);
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a graph based on sort of a "windowed" Barabasi-Albert model
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_recent_degree_game
 */
PyObject* igraphmodule_Graph_Recent_Degree(PyTypeObject *type,
					   PyObject *args,
					   PyObject *kwds) {
  igraphmodule_GraphObject *self;
  long n, m=0, window=0;
  float power=0.0, zero_appeal=0.0;
  igraph_vector_t outseq;
  PyObject *m_obj, *outpref=Py_False, *directed=Py_False;
  
  char *kwlist[] = {"n", "m", "window", "outpref", "directed", "power", "zero_appeal", NULL};
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "lOl|OOff", kwlist,
				   &n, &m_obj, &window, &outpref, &directed, &power,
				   &zero_appeal))
    return NULL;
  
  if (n<0) {
    PyErr_SetString(PyExc_ValueError, "Number of vertices must be positive.");
    return NULL;
  }
  
  // let's check whether we have a constant out-degree or a list
  if (PyInt_Check(m_obj)) {
    m=PyInt_AsLong(m_obj);
    igraph_vector_init(&outseq, 0);
  } else if (PyList_Check(m_obj)) {
    if (igraphmodule_PyList_to_vector_t(m_obj, &outseq, 1, 0)) {
      // something bad happened during conversion
      return NULL;
    }
  }
  
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (igraph_recent_degree_game(&self->g, (igraph_integer_t)n,
				  (igraph_real_t)power, (igraph_integer_t)window,
				  (igraph_integer_t)m,
				  &outseq, PyObject_IsTrue(outpref),
				  (igraph_real_t)zero_appeal,
				  PyObject_IsTrue(directed))) {
      igraphmodule_handle_igraph_error();
      igraph_vector_destroy(&outseq);
      return NULL;
    }
  }

  igraph_vector_destroy(&outseq);
  
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a ring-shaped graph
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_ring
 */
PyObject* igraphmodule_Graph_Ring(PyTypeObject *type,
					 PyObject *args,
					 PyObject *kwds) 
{
  long n;
  PyObject *directed=Py_False, *mutual=Py_False, *circular=Py_True;
  igraphmodule_GraphObject *self;
  
  char *kwlist[] = {"n", "directed", "mutual", "circular", NULL};
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "l|O!O!O!", kwlist, &n,
				   &PyBool_Type, &directed,
				   &PyBool_Type, &mutual,
				   &PyBool_Type, &circular))
    return NULL;
  
  if (n<0) {
    PyErr_SetString(PyExc_ValueError, "Number of vertices must be positive.");
    return NULL;
  }
   
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (igraph_ring(&self->g, (igraph_integer_t)n, (directed == Py_True),
		    (mutual == Py_True), (circular == Py_True))) {
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  }
   
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a tree graph where almost all vertices have an equal number of children
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_tree
 */
PyObject* igraphmodule_Graph_Tree(PyTypeObject *type,
				  PyObject *args, PyObject *kwds) {
  long n, children;
  igraph_tree_mode_t mode=IGRAPH_TREE_UNDIRECTED;
  igraphmodule_GraphObject *self;
  
  char *kwlist[] = {"n", "children", "type", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "ll|l", kwlist,
				   &n, &children, &mode))
    return NULL;
  
  if (n<0) {
    PyErr_SetString(PyExc_ValueError, "Number of vertices must be positive.");
    return NULL;
  }
   
  if (mode!=IGRAPH_TREE_UNDIRECTED && mode!=IGRAPH_TREE_IN &&
      mode!=IGRAPH_TREE_OUT) {
    PyErr_SetString(PyExc_ValueError, "Mode should be either TREE_IN, TREE_OUT or TREE_UNDIRECTED.");
    return NULL;
  }
   
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (igraph_tree(&self->g, (igraph_integer_t)n, (igraph_integer_t)children, mode)) {
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  }
   
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a random graph with a given degree sequence
 * This is intended to be a class method in Python, so the first argument
 * is the type object and not the Python igraph object (because we have
 * to allocate that in this method).
 * 
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_degree_sequence_game
 */
PyObject* igraphmodule_Graph_Degree_Sequence(PyTypeObject *type,
					     PyObject *args, PyObject *kwds) {
  igraphmodule_GraphObject *self;
  igraph_vector_t outseq, inseq;
  PyObject *outdeg=NULL, *indeg=NULL;
  
  char *kwlist[] = {"out", "in", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!|O!", kwlist,
				   &PyList_Type, &outdeg,
				   &PyList_Type, &indeg))
    return NULL;
  
  if (igraphmodule_PyList_to_vector_t(outdeg, &outseq, 1, 0)) {
    // something bad happened during conversion
    return NULL;
  }
  if (indeg) {
    if (igraphmodule_PyList_to_vector_t(indeg, &inseq, 1, 0)) {
      // something bad happened during conversion
      igraph_vector_destroy(&outseq);
      return NULL;
    }
  } else {
    igraph_vector_init(&inseq, 0);
  }
  
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (igraph_degree_sequence_game(&self->g, &outseq, &inseq,
				    IGRAPH_DEGSEQ_SIMPLE)) {
      igraphmodule_handle_igraph_error();
      igraph_vector_destroy(&outseq);
      igraph_vector_destroy(&inseq);
      return NULL;
    }
  }
   
  igraph_vector_destroy(&outseq);
  igraph_vector_destroy(&inseq);
  
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Generates a graph with a given isomorphy class
 * This is intended to be a class method in Python, so the first argument
 * is the type object and not the Python igraph object (because we have
 * to allocate that in this method).
 * 
 * \return a reference to the newly generated Python igraph object
 * \sa igraph_isoclass_create
 */
PyObject* igraphmodule_Graph_Isoclass(PyTypeObject *type,
				      PyObject *args, PyObject *kwds) {
  long n, isoclass;
  PyObject *directed=NULL;
  igraphmodule_GraphObject *self;
  
  char *kwlist[] = {"n", "class", "directed", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii|O", kwlist,
				   &n, &isoclass, &directed))
    return NULL;

  if (n<3 || n>4) {
    PyErr_SetString(PyExc_ValueError, "Only graphs with 3 or 4 vertices are supported");
    return NULL;
  }
  
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  RC_ALLOC("Graph", self);
  
  if (self != NULL) {
    igraphmodule_Graph_init_internal(self);
    if (igraph_isoclass_create(&self->g, n, isoclass, PyObject_IsTrue(directed))) {
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  }
    
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Decides whether a graph is connected.
 * \return Py_True if the graph is connected, Py_False otherwise
 * \sa igraph_is_connected
 */
PyObject* igraphmodule_Graph_is_connected(igraphmodule_GraphObject *self,
						 PyObject *args,
						 PyObject *kwds) 
{
   char *kwlist[] = {"mode", NULL};
   igraph_connectedness_t mode=IGRAPH_STRONG;
   igraph_bool_t res;
   
   if (!PyArg_ParseTupleAndKeywords(args, kwds, "|l", kwlist, &mode))
     return NULL;

   if (mode != IGRAPH_STRONG && mode != IGRAPH_WEAK) 
     {
	PyErr_SetString(PyExc_ValueError, "mode must be either STRONG or WEAK");
	return NULL;
     }
   
   if (igraph_is_connected(&self->g, &res, mode)) 
     {
	igraphmodule_handle_igraph_error();
	return NULL;
     }
   if (res) {
     Py_INCREF(Py_True); return Py_True;
   } else {
     Py_INCREF(Py_False); return Py_False;
   }
}

/** \ingroup python_interface_graph
 * \brief Decides whether there is an edge from a given vertex to an other one.
 * \return Py_True if the vertices are directly connected, Py_False otherwise
 * \sa igraph_are_connected
 */
PyObject* igraphmodule_Graph_are_connected(igraphmodule_GraphObject *self,
						  PyObject *args,
						  PyObject *kwds) 
{
   char *kwlist[] = {"v1", "v2", NULL};
   long v1, v2;
   igraph_bool_t res;
   
   if (!PyArg_ParseTupleAndKeywords(args, kwds, "ll", kwlist, &v1, &v2))
     return NULL;

   if (igraph_are_connected(&self->g, (igraph_integer_t)v1, (igraph_integer_t)v2, &res))
     return NULL;
       
   if (res) {
     Py_INCREF(Py_True); return Py_True;
   } else {
     Py_INCREF(Py_False); return Py_False;
   }
}

/** \ingroup python_interface_graph
 * \brief Calculates the average path length in a graph.
 * \return the average path length as a PyObject
 * \sa igraph_average_path_length
 */
PyObject* igraphmodule_Graph_average_path_length(igraphmodule_GraphObject *self,
							PyObject *args,
							PyObject *kwds) 
{
   char *kwlist[] = {"directed", "unconn", NULL};
   PyObject *directed=Py_True, *unconn=Py_True;
   igraph_real_t res;
   
   if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O!O!", kwlist,
				    &PyBool_Type, &directed,
				    &PyBool_Type, &unconn))
     return NULL;

   if (igraph_average_path_length(&self->g, &res, (directed==Py_True),
				  (unconn==Py_True))) 
     {
	igraphmodule_handle_igraph_error(); return NULL;
     }
     
   return PyFloat_FromDouble(res);
}

/** \ingroup python_interface_graph
 * \brief Calculates the betweennesses of some nodes in a graph.
 * \return the betweennesses as a list (or a single float)
 * \sa igraph_betweenness
 */
PyObject* igraphmodule_Graph_betweenness(igraphmodule_GraphObject *self, PyObject *args, PyObject *kwds) {
  char *kwlist[] = {"vertices", "directed", NULL};
  PyObject *directed=Py_True;
  PyObject *vobj=Py_None, *list;
  igraph_vector_t res;
  igraph_bool_t return_single=0;
  igraph_vs_t vs;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OO", kwlist,
				   &vobj, &directed)) {
    return NULL;
  }
  
  if (igraphmodule_PyObject_to_vs_t(vobj, &vs, &return_single)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
  
  if (igraph_vector_init(&res, 0)) {
    igraph_vs_destroy(&vs);
    return igraphmodule_handle_igraph_error();
  }

  if (igraph_betweenness(&self->g, &res, vs, PyObject_IsTrue(directed))) {
    igraph_vs_destroy(&vs);
    igraphmodule_handle_igraph_error(); return NULL;
  }
  
  if (!return_single)
    list=igraphmodule_vector_t_to_float_PyList(&res);
  else
    list=PyFloat_FromDouble(VECTOR(res)[0]);
  
  igraph_vector_destroy(&res);
  igraph_vs_destroy(&vs);
  
  return list;
}

/** \ingroup python_interface_graph
 * \brief Calculates the Google PageRank value of some nodes in the graph.
 * \return the PageRank values
 * \sa igraph_pagerank
 */
PyObject* igraphmodule_Graph_pagerank(igraphmodule_GraphObject *self, PyObject *args, PyObject *kwds) {
  char *kwlist[] = {"vertices", "directed", "niter", "eps", "damping", NULL};
  PyObject *directed=Py_True;
  PyObject *vobj=Py_None, *list;
  long int niter=1000; /// @todo maybe it should be selected adaptively based on the number of vertices?
  double eps=0.001, damping=0.85;
  igraph_vector_t res;
  igraph_bool_t return_single=0;
  igraph_vs_t vs;

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OOldd", kwlist,
				   &vobj, &directed, &niter, &eps, &damping))
    return NULL;

  if (igraphmodule_PyObject_to_vs_t(vobj, &vs, &return_single)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }

  if (igraph_vector_init(&res, 0)) {
    igraph_vs_destroy(&vs);
    return igraphmodule_handle_igraph_error();
  }

  if (igraph_pagerank(&self->g, &res, vs,
		      PyObject_IsTrue(directed), niter, eps, damping)) {
    igraph_vs_destroy(&vs);
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  if (!return_single)
    list=igraphmodule_vector_t_to_float_PyList(&res);
  else
    list=PyFloat_FromDouble(VECTOR(res)[0]);
   
  igraph_vector_destroy(&res);
  igraph_vs_destroy(&vs);
   
  return list;
}

/** \ingroup python_interface_graph
 * \brief Calculates the bibliographic coupling of some nodes in a graph.
 * \return the bibliographic coupling values in a matrix
 * \sa igraph_bibcoupling
 */
PyObject* igraphmodule_Graph_bibcoupling(igraphmodule_GraphObject *self,
						PyObject *args,
						PyObject *kwds) {
  char *kwlist[] = {"vertices", NULL};
  PyObject *vobj=NULL, *list;
  igraph_matrix_t res;
  igraph_vs_t vs;
  igraph_bool_t return_single=0;

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &vobj))
    return NULL;

  if (igraphmodule_PyObject_to_vs_t(vobj, &vs, &return_single)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  if (igraph_matrix_init(&res, 1, igraph_vcount(&self->g))) {
    igraph_vs_destroy(&vs);
    igraphmodule_handle_igraph_error();
    return NULL;
  }

  if (igraph_bibcoupling(&self->g, &res, vs)) {
    igraph_vs_destroy(&vs);
    igraphmodule_handle_igraph_error(); return NULL;
  }

  /* TODO: Return a single list instead of a matrix if only one vertex was given */
  list=igraphmodule_matrix_t_to_PyList(&res, IGRAPHMODULE_TYPE_INT);
   
  igraph_matrix_destroy(&res);
  igraph_vs_destroy(&vs);
   
  return list;
}

/** \ingroup python_interface_graph
 * \brief Calculates the closeness centrality of some nodes in a graph.
 * \return the closeness centralities as a list (or a single float)
 * \sa igraph_betweenness
 */
PyObject* igraphmodule_Graph_closeness(igraphmodule_GraphObject *self,
				       PyObject *args,
				       PyObject *kwds) {
  char *kwlist[] = {"vertices", "mode", NULL};
  PyObject *vobj=Py_None, *list=NULL;
  igraph_vector_t res;
  igraph_neimode_t mode=IGRAPH_ALL;
  int return_single=0;
  igraph_vs_t vs;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|Ol", kwlist,
				   &vobj, &mode))
    return NULL;
  
  if (mode != IGRAPH_OUT && mode != IGRAPH_IN && mode != IGRAPH_ALL) {
    PyErr_SetString(PyExc_ValueError, "mode must be one of IN, OUT or ALL");
    return NULL;
  }
   
  if (igraphmodule_PyObject_to_vs_t(vobj, &vs, &return_single)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
  
  if (igraph_vector_init(&res, 0)) {
    igraph_vs_destroy(&vs);
    return igraphmodule_handle_igraph_error();
  }

  if (igraph_closeness(&self->g, &res, vs, mode)) {
    igraph_vs_destroy(&vs);
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  if (!return_single)
    list=igraphmodule_vector_t_to_float_PyList(&res);
  else
    list=PyFloat_FromDouble(VECTOR(res)[0]);
  
  igraph_vector_destroy(&res);
  igraph_vs_destroy(&vs);
   
  return list;
}

/** \ingroup python_interface_graph
 * \brief Calculates the (weakly or strongly) connected components in a graph.
 * \return a list containing the cluster ID for every vertex in the graph
 * \sa igraph_clusters
 */
PyObject* igraphmodule_Graph_clusters(igraphmodule_GraphObject *self,
					     PyObject *args,
					     PyObject *kwds) 
{
   char *kwlist[] = {"mode", NULL};
   igraph_connectedness_t mode=IGRAPH_STRONG;
   igraph_vector_t res1, res2;
   igraph_integer_t no;
   PyObject *list;
   
   if (!PyArg_ParseTupleAndKeywords(args, kwds, "|l", kwlist, &mode))
     return NULL;

   if (mode != IGRAPH_STRONG && mode != IGRAPH_WEAK) 
     {
	PyErr_SetString(PyExc_ValueError, "mode must be either STRONG or WEAK");
	return NULL;
     }
   
   igraph_vector_init(&res1, igraph_vcount(&self->g));
   igraph_vector_init(&res2, 10);
   
   if (igraph_clusters(&self->g, &res1, &res2, &no, mode)) 
     {
	igraphmodule_handle_igraph_error();
	igraph_vector_destroy(&res1);
	igraph_vector_destroy(&res2);
	return NULL;
     }
   
   list=igraphmodule_vector_t_to_PyList(&res1);
   igraph_vector_destroy(&res1);
   igraph_vector_destroy(&res2);
   return list;
}

/** \ingroup python_interface_graph
 * \brief Calculates Burt's constraint scores for a given graph
 * \sa igraph_constraint
 */
PyObject* igraphmodule_Graph_constraint(igraphmodule_GraphObject *self,
					PyObject *args,
					PyObject *kwds) {
  char *kwlist[] = {"vertices", "weights", NULL};
  PyObject *vids_obj=Py_None, *weight_obj=Py_None, *list;
  igraph_vector_t result, weights;
  igraph_vs_t vids;
  igraph_bool_t return_single=0, weight_obj_was_created=0;

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OO", kwlist, &vids_obj, weight_obj))
    return NULL;
  
  if (igraph_vector_init(&result, 0)) {
    igraphmodule_handle_igraph_error();
    return NULL;
  }
  
  if (igraphmodule_PyObject_to_attribute_values(weight_obj, &weights,
						self, ATTRHASH_IDX_EDGE,
						1.0)) {
    igraph_vector_destroy(&result);
    if (weight_obj_was_created) { Py_DECREF(weight_obj); }
    return NULL;
  }
  
  if (igraphmodule_PyObject_to_vs_t(vids_obj, &vids, &return_single)) {
    igraphmodule_handle_igraph_error();
    igraph_vector_destroy(&result);
    igraph_vector_destroy(&weights);
    return NULL;
  }

  if (igraph_constraint(&self->g, &result, vids, &weights)) {
    igraphmodule_handle_igraph_error();
    igraph_vs_destroy(&vids);
    igraph_vector_destroy(&result);
    igraph_vector_destroy(&weights);
    return NULL;
  }
  
  list=igraphmodule_vector_t_to_PyList(&result);
  igraph_vs_destroy(&vids);
  igraph_vector_destroy(&result);
  igraph_vector_destroy(&weights);

  return list;
}

/** \ingroup python_interface_copy
 * \brief Creates an exact deep copy of the graph
 * \return the copy of the graph
 */
PyObject* igraphmodule_Graph_copy(igraphmodule_GraphObject *self) {
  igraphmodule_GraphObject *result;
  igraph_t g;
  
  if (igraph_copy(&g, &self->g)) {
    igraphmodule_handle_igraph_error();
    return NULL;
  }
  
  result = (igraphmodule_GraphObject*)self->ob_type->tp_alloc(self->ob_type, 0);
  igraphmodule_Graph_init_internal(result);
  result->g=g;
  RC_ALLOC("Graph", result);
  
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Decomposes a graph into components.
 * \return a list of graph objects, each containing a copy of a component in the original graph.
 * \sa igraph_components
 */
PyObject* igraphmodule_Graph_decompose(igraphmodule_GraphObject *self,
				       PyObject *args,
				       PyObject *kwds) {
  char *kwlist[] = {"mode", "maxcompno", "minelements", NULL};
  igraph_connectedness_t mode=IGRAPH_STRONG;
  PyObject *list;
  igraphmodule_GraphObject *o;
  long maxcompno=-1, minelements=-1, n, i;
  igraph_vector_ptr_t components;
  igraph_t *g;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|lll", kwlist, &mode,
				   &maxcompno, &minelements))
    return NULL;

  if (mode != IGRAPH_STRONG && mode != IGRAPH_WEAK) {
    PyErr_SetString(PyExc_ValueError, "mode must be either STRONG or WEAK");
    return NULL;
  }
  
  igraph_vector_ptr_init(&components, 3);
  if (igraph_decompose(&self->g, &components, mode, maxcompno, minelements)) {
    igraph_vector_ptr_destroy(&components);
    igraphmodule_handle_igraph_error();
    return NULL;
  }
   
  // We have to create a separate Python igraph object for every graph returned
  n=igraph_vector_ptr_size(&components);
  list=PyList_New(n);
  for (i=0; i<n; i++) {
    g=(igraph_t*)VECTOR(components)[i];
    o = (igraphmodule_GraphObject*)self->ob_type->tp_alloc(self->ob_type, 0);
    RC_ALLOC("Graph", self);
    igraphmodule_Graph_init_internal(o);
    o->g=*g;
    PyList_SET_ITEM(list, i, (PyObject*)o);
    // reference has been transferred by PyList_SET_ITEM, no need to Py_DECREF
    //
    // we mustn't call igraph_destroy here, because it would free the vertices
    // and the edges as well, but we need them in o->g. So just call free
    igraph_free(g);
  }
  
  igraph_vector_ptr_destroy(&components);
  
  return list;
}

/** \ingroup python_interface_graph
 * \brief Calculates the cocitation scores of some nodes in a graph.
 * \return the cocitation scores in a matrix
 * \sa igraph_cocitation
 */
PyObject* igraphmodule_Graph_cocitation(igraphmodule_GraphObject *self,
					PyObject *args,
					PyObject *kwds) {
  char *kwlist[] = {"vertices", NULL};
  PyObject *vobj=NULL, *list=NULL;
  igraph_matrix_t res;
  int return_single=0;
  igraph_vs_t vs;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &vobj))
    return NULL;
  
  if (igraphmodule_PyObject_to_vs_t(vobj, &vs, &return_single)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
  
  if (igraph_matrix_init(&res, 1, igraph_vcount(&self->g))) {
    igraph_vs_destroy(&vs);
    return igraphmodule_handle_igraph_error();
  }

  if (igraph_cocitation(&self->g, &res, vs)) {
    igraph_matrix_destroy(&res);
    igraph_vs_destroy(&vs);
    igraphmodule_handle_igraph_error(); return NULL;
  }

  /* TODO: Return a single list instead of a matrix if only one vertex was given */
  list=igraphmodule_matrix_t_to_PyList(&res, IGRAPHMODULE_TYPE_INT);
   
  igraph_matrix_destroy(&res);
  igraph_vs_destroy(&vs);
  
  return list;
}

/** \ingroup python_interface_graph
 * \brief Calculates the edge betweennesses in the graph
 * \return a list containing the edge betweenness for every edge
 * \sa igraph_edge_betweenness
 */
PyObject* igraphmodule_Graph_edge_betweenness(igraphmodule_GraphObject *self,
					      PyObject *args,
					      PyObject *kwds) {
  char *kwlist[] = {"directed", NULL};
  igraph_vector_t res;
  PyObject *list, *directed=Py_True;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O!", kwlist,
				   &PyBool_Type, &directed))
    return NULL;
  
  igraph_vector_init(&res, igraph_ecount(&self->g));
  
  if (igraph_edge_betweenness(&self->g, &res, (directed==Py_True))) {
    igraphmodule_handle_igraph_error();
    igraph_vector_destroy(&res);
    return NULL;
  }
  
  list=igraphmodule_vector_t_to_float_PyList(&res);
  igraph_vector_destroy(&res);
  return list;
}

/** \ingroup python_interface_graph
 * \brief Calculates the shortest paths from/to a given node in the graph
 * \return a list containing shortest paths from/to the given node
 * \sa igraph_get_shortest_paths
 */
PyObject* igraphmodule_Graph_get_shortest_paths(igraphmodule_GraphObject *self,
						       PyObject *args,
						       PyObject *kwds) 
{
   char *kwlist[] = {"v", "mode", NULL};
   igraph_vector_t *res;
   igraph_neimode_t mode=IGRAPH_OUT;
   long from0, i, j;
   igraph_integer_t from;
   PyObject *list, *item;
   long int no_of_nodes=igraph_vcount(&self->g);
   igraph_vector_ptr_t ptrvec;
   
   if (!PyArg_ParseTupleAndKeywords(args, kwds, "l|l", kwlist,
				    &from0, &mode))
     return NULL;

   from=(igraph_integer_t)from0;
   
   res=(igraph_vector_t*)calloc(no_of_nodes, sizeof(igraph_vector_t));
   if (!res) 
     {
	PyErr_SetString(PyExc_MemoryError, "");
	return NULL;
     }

   if (igraph_vector_ptr_init(&ptrvec, no_of_nodes)) 
     {
       PyErr_SetString(PyExc_MemoryError, "");
       return NULL;
     }  
   
   for (i=0; i<no_of_nodes; i++)
     {
       VECTOR(ptrvec)[i]=&res[i];
       igraph_vector_init(&res[i], 5);
     }
   
   if (igraph_get_shortest_paths(&self->g, &ptrvec, from, 
				 igraph_vss_all(), mode))
     {
	igraphmodule_handle_igraph_error();
	for (j=0; j<no_of_nodes; j++) igraph_vector_destroy(&res[j]);
	free(res);
	return NULL;
     }

   list=PyList_New(no_of_nodes);
   if (!list) {
      for (j=0; j<no_of_nodes; j++) igraph_vector_destroy(&res[j]);
      free(res);
      return NULL;
   }
   
   for (i=0; i<no_of_nodes; i++) 
     {
	item=igraphmodule_vector_t_to_PyList(&res[i]);
	if (!item) 
	  {
	     Py_DECREF(list);
	     for (j=0; j<no_of_nodes; j++) igraph_vector_destroy(&res[j]);
	     free(res);
	     return NULL;
	  }
	if (PyList_SetItem(list, i, item)) 
	  {
	     Py_DECREF(list);
	     for (j=0; j<no_of_nodes; j++) igraph_vector_destroy(&res[j]);
	     free(res);
	     return NULL;
	  }
     }
   
   for (j=0; j<no_of_nodes; j++) igraph_vector_destroy(&res[j]);
   free(res);
   igraph_vector_ptr_destroy(&ptrvec);
   return list;
}

/** \ingroup python_interface_graph
 * \brief Calculates all of the shortest paths from/to a given node in the graph
 * \return a list containing shortest paths from/to the given node
 * \sa igraph_get_shortest_paths
 */
PyObject* igraphmodule_Graph_get_all_shortest_paths(igraphmodule_GraphObject *self,
						       PyObject *args,
						       PyObject *kwds) 
{
  char *kwlist[] = {"v", "mode", NULL};
  igraph_vector_ptr_t res;
  igraph_neimode_t mode=IGRAPH_OUT;
  long from0, i, j, k;
  igraph_integer_t from;
  PyObject *list, *item;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "l|l", kwlist,
				   &from0, &mode))
    return NULL;
  
  from=(igraph_integer_t)from0;
  
  if (igraph_vector_ptr_init(&res, 1)) {
    igraphmodule_handle_igraph_error();
    return NULL;
  }
  
  if (igraph_get_all_shortest_paths(&self->g, &res, NULL, from, 
				    igraph_vss_all(), mode)) {
    igraphmodule_handle_igraph_error();
    igraph_vector_ptr_destroy(&res);
    return NULL;
  }
  
  j=igraph_vector_ptr_size(&res);
  list=PyList_New(j);
  if (!list) {
    for (i=0; i<j; i++) igraph_vector_destroy(igraph_vector_ptr_e(&res, i));
    igraph_vector_ptr_destroy_all(&res);
    return NULL;
  }
   
  for (i=0; i<j; i++) {
    item=igraphmodule_vector_t_to_PyList((igraph_vector_t*)igraph_vector_ptr_e(&res, i));
    if (!item) {
      Py_DECREF(list);
      for (k=0; k<j; k++) igraph_vector_destroy(igraph_vector_ptr_e(&res, k));
      igraph_vector_ptr_destroy_all(&res);
      return NULL;
    }
    if (PyList_SetItem(list, i, item)) {
      Py_DECREF(list);
      Py_DECREF(item);
      for (k=0; k<j; k++) igraph_vector_destroy(igraph_vector_ptr_e(&res, k));
      igraph_vector_ptr_destroy_all(&res);
      return NULL;
    }
  }
  
  for (i=0; i<j; i++) igraph_vector_destroy(igraph_vector_ptr_e(&res, i));
  igraph_vector_ptr_destroy_all(&res);
  return list;
}

/** \ingroup python_interface_graph
 * \brief Calculates shortest paths in a graph.
 * \return the shortest path lengths for the given vertices
 * \sa igraph_shortest_paths
 */
PyObject* igraphmodule_Graph_shortest_paths(igraphmodule_GraphObject *self,
					    PyObject *args,
					    PyObject *kwds) {
  char *kwlist[] = {"vertices", "mode", NULL};
  PyObject *vobj=NULL, *list=NULL;
  igraph_matrix_t res;
  igraph_neimode_t mode=IGRAPH_OUT;
  int return_single=0;
  igraph_vs_t vs;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|Ol", kwlist, &vobj, &mode))
    return NULL;
  
  if (mode!=IGRAPH_IN && mode!=IGRAPH_OUT && mode!=IGRAPH_ALL) {
    PyErr_SetString(PyExc_ValueError, "mode must be either IN or OUT or ALL");
    return NULL;
  }
  
  if (igraphmodule_PyObject_to_vs_t(vobj, &vs, &return_single)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
  
  if (igraph_matrix_init(&res, 1, igraph_vcount(&self->g))) {
    igraph_vs_destroy(&vs);
    return igraphmodule_handle_igraph_error();
  }

  if (igraph_shortest_paths(&self->g, &res, vs, mode)) {
    igraph_matrix_destroy(&res);
    igraph_vs_destroy(&vs);
    igraphmodule_handle_igraph_error(); return NULL;
  }

  /* TODO Return a single list instead of a matrix if only one vertex was given */
  list=igraphmodule_matrix_t_to_PyList(&res, IGRAPHMODULE_TYPE_INT);
   
  igraph_matrix_destroy(&res);
  igraph_vs_destroy(&vs);
   
  return list;
}

/** \ingroup python_interface_graph
 * \brief Calculates a spanning tree for a graph
 * \return a list containing the edge betweenness for every edge
 * \sa igraph_minimum_spanning_tree_unweighted
 * \sa igraph_minimum_spanning_tree_prim
 */
PyObject* igraphmodule_Graph_spanning_tree(igraphmodule_GraphObject *self,
						  PyObject *args,
						  PyObject *kwds) 
{
  char *kwlist[] = {"weights", NULL};
  igraph_t mst;
  int err;
  igraph_vector_t ws;
  PyObject *weights=NULL;
  igraphmodule_GraphObject *result=NULL;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O!", kwlist,
				   &PyList_Type, &weights))
    return NULL;
  
  if (weights && (PyList_Size(weights)<igraph_vcount(&self->g))) {
    PyErr_SetString(PyExc_ValueError, "Weight list must have at least |V| elements (|V| = node count in the graph)");
    return NULL;
  }

  if (!weights)
    err=igraph_minimum_spanning_tree_unweighted(&self->g, &mst);
  else {
    if (igraphmodule_PyList_to_vector_t(weights, &ws, 1, 0)) return NULL;
    err=igraph_minimum_spanning_tree_prim(&self->g, &mst, &ws);
  }
   
  if (err) {
    igraphmodule_handle_igraph_error();
    if (weights) igraph_vector_destroy(&ws);
    return NULL;
  }

  result = (igraphmodule_GraphObject*)self->ob_type->tp_alloc(self->ob_type, 0);
  RC_ALLOC("Graph", result);
  
  if (result != NULL) result->g=mst;
   
  if (weights) igraph_vector_destroy(&ws);
   
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Simplifies a graph by removing loops and/or multiple edges
 * \return the simplified graph.
 * \sa igraph_simplify
 */
PyObject* igraphmodule_Graph_simplify(igraphmodule_GraphObject *self,
					     PyObject *args,
					     PyObject *kwds) 
{
   char *kwlist[] = {"multiple", "loops", NULL};
   PyObject *multiple=Py_True, *loops=Py_True;
   
   if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OO", kwlist,
				    &multiple, &loops))
     return NULL;

   if (igraph_simplify(&self->g, PyObject_IsTrue(multiple),
		       PyObject_IsTrue(loops)))
     {
	igraphmodule_handle_igraph_error();
	return NULL;
     }

   Py_INCREF(self);
   return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Calculates the vertex indices within the same component as a given vertex
 * \return the vertex indices in a list
 * \sa igraph_subcomponent
 */
PyObject* igraphmodule_Graph_subcomponent(igraphmodule_GraphObject *self,
					     PyObject *args,
					     PyObject *kwds) 
{
   char *kwlist[] = {"v", "mode", NULL};
   igraph_vector_t res;
   igraph_neimode_t mode=IGRAPH_ALL;
   long from0;
   igraph_real_t from;
   PyObject *list=NULL;
   
   if (!PyArg_ParseTupleAndKeywords(args, kwds, "l|l", kwlist,
				    &from0, &mode))
     return NULL;

   if (mode != IGRAPH_OUT && mode != IGRAPH_IN && mode != IGRAPH_ALL) 
     {
	PyErr_SetString(PyExc_ValueError, "mode must be either IN, OUT or ALL");
	return NULL;
     }
   
   if (from0<0 || from0>=igraph_vcount(&self->g)) 
     {
	PyErr_SetString(PyExc_ValueError, "vertex ID must be non-negative and less than the number of edges");
	return NULL;
     }
   from=(igraph_real_t)from0;

   igraph_vector_init(&res, 0);
   if (igraph_subcomponent(&self->g, &res, from, mode))
     {
	igraphmodule_handle_igraph_error();
	igraph_vector_destroy(&res);
	return NULL;
     }

   list=igraphmodule_vector_t_to_PyList(&res);
   igraph_vector_destroy(&res);
   
   return list;
}

/** \ingroup python_interface_graph
 * \brief Rewires a graph while preserving degree distribution
 * \return the rewired graph
 * \sa igraph_rewire
 */
PyObject* igraphmodule_Graph_rewire(igraphmodule_GraphObject *self,
				    PyObject *args,
				    PyObject *kwds) {
  char *kwlist[] = {"n", "mode", NULL};
  long n=1000;
  igraph_rewiring_t mode = IGRAPH_REWIRING_SIMPLE;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ll", kwlist,
				   &n, &mode)) return NULL;
  
   if (mode!=IGRAPH_REWIRING_SIMPLE) {
     PyErr_SetString(PyExc_ValueError, "mode must be REWIRING_SIMPLE");
     return NULL;
   }
   
  if (igraph_rewire(&self->g, n, mode)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
  
  Py_INCREF(self);
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Returns a subgraph of the graph based on the given vertices
 * \return the subgraph as a new igraph object
 * \sa igraph_subgraph
 */
PyObject* igraphmodule_Graph_subgraph(igraphmodule_GraphObject *self,
					     PyObject *args,
					     PyObject *kwds) {
  char *kwlist[] = {"vertices", NULL};
  igraph_vector_t vertices;
  igraph_t sg;
  igraphmodule_GraphObject *result;
  PyObject *list;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O", kwlist, &list))
    return NULL;

  if (igraphmodule_PyList_to_vector_t(list, &vertices, 1, 0))
    return NULL;
  
  if (igraph_subgraph(&self->g, &sg, igraph_vss_vector(&vertices))) {
    igraphmodule_handle_igraph_error();
    igraph_vector_destroy(&vertices);
    return NULL;
  }

  result = (igraphmodule_GraphObject*)self->ob_type->tp_alloc(self->ob_type, 0);
  RC_ALLOC("Graph", result);
  if (result != NULL) result->g=sg;
  
  igraph_vector_destroy(&vertices);
  
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Calculates the graph transitivity (a.k.a. clustering coefficient)
 * \return the clustering coefficient
 * \sa igraph_transitivity_undirected
 */
PyObject* igraphmodule_Graph_transitivity_undirected(igraphmodule_GraphObject *self,
						     PyObject *args,
						     PyObject *kwds) {
  igraph_real_t res;

  if (igraph_transitivity_undirected(&self->g, &res)) {
    igraphmodule_handle_igraph_error();
    return NULL;
  }

  PyObject *r = Py_BuildValue("d", (double)(res));
  return r;
}

/** \ingroup python_interface_graph
 * \brief Calculates the local transitivity of given vertices
 * \return the transitivities in a list
 * \sa igraph_transitivity_local_undirected
 */
PyObject* igraphmodule_Graph_transitivity_local_undirected(igraphmodule_GraphObject *self,
							   PyObject *args,
							   PyObject *kwds) {
  char *kwlist[] = {"vertices", NULL};
  PyObject *vobj = NULL, *list = NULL;
  igraph_vector_t result;
  igraph_bool_t return_single=0;
  igraph_vs_t vs;

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &vobj))
    return NULL;

  if (igraphmodule_PyObject_to_vs_t(vobj, &vs, &return_single)) {
    igraphmodule_handle_igraph_error();
    return NULL;
  }

  if (igraph_vector_init(&result, 0)) {
    igraph_vs_destroy(&vs);
    return igraphmodule_handle_igraph_error();
  }

  if (igraph_transitivity_local_undirected(&self->g, &result, vs)) {
    igraphmodule_handle_igraph_error();
    igraph_vs_destroy(&vs);
    igraph_vector_destroy(&result);
    return NULL;
  }

  if (!return_single)
    list=igraphmodule_vector_t_to_float_PyList(&result);
  else
    list=PyFloat_FromDouble(VECTOR(result)[0]);

  igraph_vs_destroy(&vs);
  igraph_vector_destroy(&result);

  return list;
}

/** \ingroup python_interface_graph
 * \brief Calculates the graph reciprocity
 * \return the reciprocity
 * \sa igraph_reciprocity
 */
PyObject* igraphmodule_Graph_reciprocity(igraphmodule_GraphObject *self,
					 PyObject *args, PyObject *kwds) {
  char *kwlist[] = {"ignore_loops", NULL};
  igraph_real_t result;
  PyObject *ignore_loops = Py_True;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &ignore_loops))
    return NULL;

  if (igraph_reciprocity(&self->g, &result, PyObject_IsTrue(ignore_loops))) {
    igraphmodule_handle_igraph_error();
    return NULL;
  }

  return Py_BuildValue("d", (double)result);
}

/** \ingroup python_interface_graph
 * \brief Calculates the graph density
 * \return the density
 * \sa igraph_density
 */
PyObject* igraphmodule_Graph_density(igraphmodule_GraphObject *self,
				     PyObject *args, PyObject *kwds) {
  char *kwlist[] = {"loops", NULL};
  igraph_real_t result;
  PyObject *loops = Py_False;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &loops))
    return NULL;

  if (igraph_density(&self->g, &result, PyObject_IsTrue(loops))) {
    igraphmodule_handle_igraph_error();
    return NULL;
  }

  return Py_BuildValue("d", (double)result);
}

/** \ingroup python_interface_graph
 * \brief Places the vertices of a graph uniformly on a circle.
 * \return the calculated coordinates as a Python list of lists
 * \sa igraph_layout_circle
 */
PyObject* igraphmodule_Graph_layout_circle(igraphmodule_GraphObject *self,
						  PyObject *args,
						  PyObject *kwds) 
{
   igraph_matrix_t m;
   PyObject *result;
   
   if (igraph_matrix_init(&m, 1, 1)) 
     {
	igraphmodule_handle_igraph_error(); return NULL;
     }
   
   if (igraph_layout_circle(&self->g, &m))
     {
	igraph_matrix_destroy(&m);
	igraphmodule_handle_igraph_error(); return NULL;
     }
   
   result=igraphmodule_matrix_t_to_PyList(&m, IGRAPHMODULE_TYPE_FLOAT);
   
   igraph_matrix_destroy(&m);
   
   return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Places the vertices of a graph uniformly on a sphere in 3D.
 * \return the calculated coordinates as a Python list of lists
 * \sa igraph_layout_sphere
 */
PyObject* igraphmodule_Graph_layout_sphere(igraphmodule_GraphObject *self,
					   PyObject *args,
					   PyObject *kwds) 
{
   igraph_matrix_t m;
   PyObject *result;
   
   if (igraph_matrix_init(&m, 1, 1)) 
     {
	igraphmodule_handle_igraph_error(); return NULL;
     }
   
   if (igraph_layout_sphere(&self->g, &m))
     {
	igraph_matrix_destroy(&m);
	igraphmodule_handle_igraph_error(); return NULL;
     }
   
   result=igraphmodule_matrix_t_to_PyList(&m, IGRAPHMODULE_TYPE_FLOAT);
   
   igraph_matrix_destroy(&m);
   
   return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Places the vertices of a graph randomly.
 * \return the calculated coordinates as a Python list of lists
 * \sa igraph_layout_random
 */
PyObject* igraphmodule_Graph_layout_random(igraphmodule_GraphObject *self,
					   PyObject *args,
					   PyObject *kwds) 
{
   igraph_matrix_t m;
   PyObject *result;
   
   if (igraph_matrix_init(&m, 1, 1)) 
     {
	igraphmodule_handle_igraph_error(); return NULL;
     }
   
   if (igraph_layout_random(&self->g, &m))
     {
	igraph_matrix_destroy(&m);
	igraphmodule_handle_igraph_error(); return NULL;
     }
   
   result=igraphmodule_matrix_t_to_PyList(&m, IGRAPHMODULE_TYPE_FLOAT);   
   igraph_matrix_destroy(&m);
   return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Places the vertices of a graph randomly in 3D.
 * \return the calculated coordinates as a Python list of lists
 * \sa igraph_layout_random_3d
 */
PyObject* igraphmodule_Graph_layout_random_3d(igraphmodule_GraphObject *self,
					      PyObject *args,
					      PyObject *kwds) 
{
   igraph_matrix_t m;
   PyObject *result;
   
   if (igraph_matrix_init(&m, 1, 1)) 
     {
	igraphmodule_handle_igraph_error(); return NULL;
     }
   
   if (igraph_layout_random_3d(&self->g, &m))
     {
	igraph_matrix_destroy(&m);
	igraphmodule_handle_igraph_error(); return NULL;
     }
   
   result=igraphmodule_matrix_t_to_PyList(&m, IGRAPHMODULE_TYPE_FLOAT);   
   igraph_matrix_destroy(&m);
   return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Places the vertices on a plane according to the Kamada-Kawai algorithm.
 * \return the calculated coordinates as a Python list of lists
 * \sa igraph_layout_kamada_kawai
 */
PyObject* igraphmodule_Graph_layout_kamada_kawai(igraphmodule_GraphObject *self,
							PyObject *args,
							PyObject *kwds) 
{
  char *kwlist[] = {"maxiter", "sigma", "initemp", "coolexp", "kkconst", NULL};
  igraph_matrix_t m;
  long niter=1000;
  double sigma, initemp, coolexp, kkconst;
  PyObject *result;
   
  sigma=igraph_vcount(&self->g);
  kkconst=sigma*sigma; sigma=sigma/4.0;
  initemp=10.0; coolexp=0.99;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ldddd", kwlist,
				   &niter, &sigma, &initemp, &coolexp, &kkconst))
    return NULL;
  
  if (igraph_matrix_init(&m, 1, 1)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  if (igraph_layout_kamada_kawai(&self->g, &m, niter, sigma, initemp, coolexp, kkconst)) {
    igraph_matrix_destroy(&m);
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  result=igraphmodule_matrix_t_to_PyList(&m, IGRAPHMODULE_TYPE_FLOAT);   
  igraph_matrix_destroy(&m);
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Places the vertices on a plane according to the Kamada-Kawai algorithm in 3D.
 * \return the calculated coordinates as a Python list of lists
 * \sa igraph_layout_kamada_kawai_3d
 */
PyObject* igraphmodule_Graph_layout_kamada_kawai_3d(igraphmodule_GraphObject *self,
						    PyObject *args,
						    PyObject *kwds) 
{
  char *kwlist[] = {"maxiter", "sigma", "initemp", "coolexp", "kkconst", NULL};
  igraph_matrix_t m;
  long niter=1000;
  double sigma, initemp, coolexp, kkconst;
  PyObject *result;
   
  sigma=igraph_vcount(&self->g);
  kkconst=sigma*sigma; sigma=sigma/4.0;
  initemp=10.0; coolexp=0.99;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ldddd", kwlist,
				   &niter, &sigma, &initemp, &coolexp, &kkconst))
    return NULL;
  
  if (igraph_matrix_init(&m, 1, 1)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  if (igraph_layout_kamada_kawai_3d(&self->g, &m, niter, sigma, initemp, coolexp, kkconst)) {
    igraph_matrix_destroy(&m);
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  result=igraphmodule_matrix_t_to_PyList(&m, IGRAPHMODULE_TYPE_FLOAT);   
  igraph_matrix_destroy(&m);
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Places the vertices on a plane according to the Fruchterman-Reingold algorithm.
 * \return the calculated coordinates as a Python list of lists
 * \sa igraph_layout_fruchterman_reingold
 */
PyObject* igraphmodule_Graph_layout_fruchterman_reingold(igraphmodule_GraphObject *self,
							 PyObject *args,
							 PyObject *kwds) {
  char *kwlist[] = {"maxiter", "maxdelta", "area", "coolexp", "repulserad", NULL};
  igraph_matrix_t m;
  long niter=500;
  double maxdelta, area, coolexp, repulserad;
  PyObject *result;
   
  maxdelta=igraph_vcount(&self->g);
  area=maxdelta*maxdelta; coolexp=1.5;
  repulserad=area*maxdelta;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ldddd", kwlist,
				   &niter, &maxdelta, &area, &coolexp, &repulserad))
    return NULL;
  
  if (igraph_matrix_init(&m, 1, 1)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  if (igraph_layout_fruchterman_reingold(&self->g, &m, niter, maxdelta, area, coolexp, repulserad, 0)) {
    igraph_matrix_destroy(&m);
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  result=igraphmodule_matrix_t_to_PyList(&m, IGRAPHMODULE_TYPE_FLOAT);   
  igraph_matrix_destroy(&m);
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Places the vertices on a plane according to the Fruchterman-Reingold algorithm in 3D.
 * \return the calculated coordinates as a Python list of lists
 * \sa igraph_layout_fruchterman_reingold_3d
 */
PyObject* igraphmodule_Graph_layout_fruchterman_reingold_3d(igraphmodule_GraphObject *self,
							    PyObject *args,
							    PyObject *kwds) {
  char *kwlist[] = {"maxiter", "maxdelta", "area", "coolexp", "repulserad", NULL};
  igraph_matrix_t m;
  long niter=500;
  double maxdelta, area, coolexp, repulserad;
  PyObject *result;
   
  maxdelta=igraph_vcount(&self->g);
  area=maxdelta*maxdelta; coolexp=1.5;
  repulserad=area*maxdelta;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ldddd", kwlist,
				   &niter, &maxdelta, &area, &coolexp, &repulserad))
    return NULL;
  
  if (igraph_matrix_init(&m, 1, 1)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  if (igraph_layout_fruchterman_reingold_3d(&self->g, &m, niter, maxdelta, area, coolexp, repulserad, 0)) {
    igraph_matrix_destroy(&m);
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  result=igraphmodule_matrix_t_to_PyList(&m, IGRAPHMODULE_TYPE_FLOAT);   
  igraph_matrix_destroy(&m);
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Places the vertices on a plane according to the Fruchterman-Reingold grid layout algorithm.
 * \return the calculated coordinates as a Python list of lists
 * \sa igraph_layout_grid_fruchterman_reingold
 */
PyObject* igraphmodule_Graph_layout_grid_fruchterman_reingold(igraphmodule_GraphObject *self,
							      PyObject *args,
							      PyObject *kwds) {
  char *kwlist[] = {"maxiter", "maxdelta", "area", "coolexp", "repulserad", "cellsize", NULL};
  igraph_matrix_t m;
  long niter=500;
  double maxdelta, area, coolexp, repulserad, cellsize;
  PyObject *result;
   
  maxdelta=igraph_vcount(&self->g);
  area=maxdelta*maxdelta; coolexp=1.5;
  repulserad=area*maxdelta;
  cellsize=1.0; // TODO: reasonable default
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|lddddd", kwlist,
				   &niter, &maxdelta, &area, &coolexp,
				   &repulserad, &cellsize))
    return NULL;
  
  if (igraph_matrix_init(&m, 1, 1)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  if (igraph_layout_grid_fruchterman_reingold(&self->g, &m, niter, maxdelta, area, coolexp, repulserad, cellsize, 0)) {
    igraph_matrix_destroy(&m);
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  result=igraphmodule_matrix_t_to_PyList(&m, IGRAPHMODULE_TYPE_FLOAT);   
  igraph_matrix_destroy(&m);
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Places the vertices of a graph according to the Large Graph Layout
 * \return the calculated coordinates as a Python list of lists
 * \sa igraph_layout_lgl
 */
PyObject* igraphmodule_Graph_layout_lgl(igraphmodule_GraphObject *self,
					PyObject *args,
					PyObject *kwds) 
{
  char *kwlist[] = {"maxiter", "maxdelta", "area", "coolexp", "repulserad", "cellsize", "proot", NULL};
  igraph_matrix_t m;
  PyObject *result;
  long maxiter=500, proot=-1;
  double maxdelta, area, coolexp, repulserad, cellsize;
   
  maxdelta=igraph_vcount(&self->g);
  area=maxdelta*maxdelta; coolexp=1.5;
  repulserad=area*maxdelta;
  cellsize=1.0; // TODO: reasonable default should be set
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ldddddl", kwlist,
				   &maxiter, &maxdelta, &area, &coolexp,
				   &repulserad, &cellsize, &proot))
    return NULL;
  
  if (igraph_matrix_init(&m, 1, 1)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
  
  if (igraph_layout_lgl(&self->g, &m, maxiter, maxdelta,
			area, coolexp, repulserad, cellsize, proot)) {
    igraph_matrix_destroy(&m);
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  result=igraphmodule_matrix_t_to_PyList(&m, IGRAPHMODULE_TYPE_FLOAT);   
  igraph_matrix_destroy(&m);
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Places the vertices of a graph according to the Reingold-Tilford
 * tree layout algorithm
 * \return the calculated coordinates as a Python list of lists
 * \sa igraph_layout_reingold_tilford
 */
PyObject* igraphmodule_Graph_layout_reingold_tilford(igraphmodule_GraphObject *self,
					PyObject *args,
					PyObject *kwds) 
{
  char *kwlist[] = {"root", NULL};
  igraph_matrix_t m;
  long int root=0;
  PyObject *result;
   
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|l", kwlist,
				   &root))
    return NULL;
  
  if (igraph_matrix_init(&m, 1, 1)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
  
  if (igraph_layout_reingold_tilford(&self->g, &m, root)) {
    igraph_matrix_destroy(&m);
    igraphmodule_handle_igraph_error(); return NULL;
  }
   
  result=igraphmodule_matrix_t_to_PyList(&m, IGRAPHMODULE_TYPE_FLOAT);   
  igraph_matrix_destroy(&m);
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Returns the adjacency matrix of a graph.
 * \return the adjacency matrix as a Python list of lists
 * \sa igraph_get_adjacency
 */
PyObject* igraphmodule_Graph_get_adjacency(igraphmodule_GraphObject *self,
						  PyObject *args,
						  PyObject *kwds) 
{
   char *kwlist[] = {"type", NULL};
   igraph_get_adjacency_t t=IGRAPH_GET_ADJACENCY_BOTH;
   igraph_matrix_t m;
   PyObject *result;
   
   if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i", kwlist, &t)) return NULL;
   
   if (t!=IGRAPH_GET_ADJACENCY_UPPER && t!=IGRAPH_GET_ADJACENCY_LOWER &&
       t!=IGRAPH_GET_ADJACENCY_BOTH)
     {
	PyErr_SetString(PyExc_ValueError, "type must be either GET_ADJACENCY_LOWER or GET_ADJACENCY_UPPER or GET_ADJACENCY_BOTH");
	return NULL;
     }
   
   if (igraph_matrix_init(&m, igraph_vcount(&self->g), igraph_vcount(&self->g))) 
     {
	igraphmodule_handle_igraph_error(); return NULL;
     }
   
   if (igraph_get_adjacency(&self->g, &m, t)) 
     {
	igraphmodule_handle_igraph_error();
	igraph_matrix_destroy(&m);
	return NULL;
     }
   
   result=igraphmodule_matrix_t_to_PyList(&m, IGRAPHMODULE_TYPE_INT);
   igraph_matrix_destroy(&m);
   return result;
}

/** \ingroup python_interface_graph
 * \brief Returns the Laplacian matrix of a graph.
 * \return the Laplacian matrix as a Python list of lists
 * \sa igraph_laplacian
 */
PyObject* igraphmodule_Graph_laplacian(igraphmodule_GraphObject *self,
				       PyObject *args,
				       PyObject *kwds) 
{
   char *kwlist[] = {"normalized", NULL};
   igraph_matrix_t m;
   PyObject *result;
   PyObject *normalized=Py_False;

   if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &normalized))
     return NULL;
   
   if (igraph_matrix_init(&m, igraph_vcount(&self->g), igraph_vcount(&self->g))) 
     {
       igraphmodule_handle_igraph_error(); return NULL;
     }
   
   if (igraph_laplacian(&self->g, &m, PyObject_IsTrue(normalized))) {
     igraphmodule_handle_igraph_error();
     igraph_matrix_destroy(&m);
     return NULL;
   }
   
   if (PyObject_IsTrue(normalized)) {
     result=igraphmodule_matrix_t_to_PyList(&m, IGRAPHMODULE_TYPE_FLOAT);
   } else {
     result=igraphmodule_matrix_t_to_PyList(&m, IGRAPHMODULE_TYPE_INT);
   }
   igraph_matrix_destroy(&m);
   return result;
}

/** \ingroup python_interface_graph
 * \brief Returns the list of edges in a graph.
 * \return the list of edges, every edge is represented by a pair
 * \sa igraph_get_edgelist
 */
PyObject* igraphmodule_Graph_get_edgelist(igraphmodule_GraphObject *self,
						 PyObject *args,
						 PyObject *kwds) 
{
   igraph_vector_t edgelist;
   PyObject *result;
   
   igraph_vector_init(&edgelist, igraph_ecount(&self->g));
   if (igraph_get_edgelist(&self->g, &edgelist, 0))
     {
	igraphmodule_handle_igraph_error();
	igraph_vector_destroy(&edgelist);
	return NULL;
     }
   
   result=igraphmodule_vector_t_to_PyList_pairs(&edgelist);
   igraph_vector_destroy(&edgelist);
   
   return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \function igraphmodule_Graph_to_undirected
 * \brief Converts a directed graph to an undirected one.
 * \return The undirected graph.
 * \sa igraph_to_undirected
 */
PyObject* igraphmodule_Graph_to_undirected(igraphmodule_GraphObject *self,
					   PyObject *args,
					   PyObject *kwds) {
  PyObject *collapse=Py_True;
  igraph_to_undirected_t mode=IGRAPH_TO_UNDIRECTED_COLLAPSE;
  static char *kwlist[] = { "collapse", NULL };
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &collapse))
    return NULL;
  mode=(PyObject_IsTrue(collapse) ? IGRAPH_TO_UNDIRECTED_COLLAPSE : IGRAPH_TO_UNDIRECTED_EACH);
  if (igraph_to_undirected(&self->g, mode)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
  Py_RETURN_NONE;
}


/** \ingroup python_interface_graph
 * \function igraphmodule_Graph_to_directed
 * \brief Converts an undirected graph to a directed one.
 * \return The directed graph.
 * \sa igraph_to_directed
 */
PyObject* igraphmodule_Graph_to_directed(igraphmodule_GraphObject *self,
					 PyObject *args,
					 PyObject *kwds) {
  PyObject *mutual=Py_True;
  igraph_to_directed_t mode=IGRAPH_TO_DIRECTED_MUTUAL;
  static char *kwlist[] = { "mutual", NULL };
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &mutual))
    return NULL;
  mode=(PyObject_IsTrue(mutual) ? IGRAPH_TO_DIRECTED_MUTUAL : IGRAPH_TO_DIRECTED_ARBITRARY);
  if (igraph_to_directed(&self->g, mode)) {
    igraphmodule_handle_igraph_error(); return NULL;
  }
  Py_RETURN_NONE;
}

/** \ingroup python_interface_graph
 * \brief Reads a DIMACS file and creates a graph from it.
 * \return the graph
 * \sa igraph_read_graph_graphml
 */
PyObject* igraphmodule_Graph_Read_DIMACS(PyTypeObject *type,
					 PyObject *args, PyObject *kwds) {
  igraphmodule_GraphObject *self;
  char* fname=NULL;
  FILE* f;
  igraph_integer_t source=0, target=0;
  igraph_vector_t capacity;
  igraph_t g;
  PyObject *directed=Py_False, *capacity_obj;
  
  char *kwlist[] = {"f", "directed", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|O", kwlist, &fname, &directed))
     return NULL;

  f=fopen(fname, "r");
  if (!f) {
    PyErr_SetString(PyExc_IOError, strerror(errno));
    return NULL;
  }
  if (igraph_vector_init(&capacity, 0)) {
    igraphmodule_handle_igraph_error();
    fclose(f);
    return NULL;
  }

  if (igraph_read_graph_dimacs(&g, f, &source, &target, &capacity,
			       PyObject_IsTrue(directed))) {
    igraphmodule_handle_igraph_error();
    igraph_vector_destroy(&capacity);
    fclose(f);
    return NULL;
  }

  capacity_obj=igraphmodule_vector_t_to_float_PyList(&capacity);
  if (!capacity_obj) {
    igraph_vector_destroy(&capacity);
    fclose(f);
    return NULL;
  }

  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  if (self != NULL) {
    RC_ALLOC("Graph", self);
    igraphmodule_Graph_init_internal(self);
    self->g=g;
  }
  fclose(f);
  igraph_vector_destroy(&capacity);

  return Py_BuildValue("NiiN", (PyObject*)self, (long)source,
		       (long)target, capacity_obj);
}

/** \ingroup python_interface_graph
 * \brief Reads an edge list from a file and creates a graph from it.
 * \return the graph
 * \sa igraph_read_graph_edgelist
 */
PyObject* igraphmodule_Graph_Read_Edgelist(PyTypeObject *type,
					   PyObject *args, PyObject *kwds) {
  igraphmodule_GraphObject *self;
  char* fname=NULL;
  FILE* f;
  PyObject *directed=Py_True;
  igraph_t g;
  
  char *kwlist[] =
  {
    "f", "directed", NULL
  };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|O", kwlist,
				   &fname, &directed))
     return NULL;

  f=fopen(fname, "r");
  if (!f) {
    PyErr_SetString(PyExc_IOError, strerror(errno));
    return NULL;
  }
  if (igraph_read_graph_edgelist(&g, f, 0, PyObject_IsTrue(directed))) {
    igraphmodule_handle_igraph_error();
    fclose(f);
    return NULL;
  }
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  if (self != NULL) {
    RC_ALLOC("Graph", self);
    igraphmodule_Graph_init_internal(self);
    self->g=g;
  }
  fclose(f);
   
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Reads an edge list from an NCOL file and creates a graph from it.
 * \return the graph
 * \sa igraph_read_graph_ncol
 */
PyObject* igraphmodule_Graph_Read_Ncol(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  igraphmodule_GraphObject *self;
  char* fname=NULL;
  FILE* f;
  PyObject *names=Py_True, *weights=Py_True, *directed=Py_True;
  igraph_t g;
  
  char *kwlist[] =
  {
    "f", "names", "weights", "directed", NULL
  };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|OOO", kwlist,
				   &fname, &names, &weights, &directed))
     return NULL;
      
  f=fopen(fname, "r");
  if (!f) {
    PyErr_SetString(PyExc_IOError, strerror(errno));
    return NULL;
  }
  if (igraph_read_graph_ncol(&g, f, 0, PyObject_IsTrue(names), PyObject_IsTrue(weights), PyObject_IsTrue(directed))) {
    igraphmodule_handle_igraph_error();
    fclose(f);
    return NULL;
  }
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  if (self != NULL) {
    RC_ALLOC("Graph", self);
    igraphmodule_Graph_init_internal(self);
    self->g=g;
  }
  fclose(f);
  
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Reads an edge list from an LGL file and creates a graph from it.
 * \return the graph
 * \sa igraph_read_graph_lgl
 */
PyObject* igraphmodule_Graph_Read_Lgl(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  igraphmodule_GraphObject *self;
  char* fname=NULL;
  FILE* f;
  PyObject *names=Py_True, *weights=Py_True;
  igraph_t g;
  
  char *kwlist[] =
  {
    "f", "names", "weights", NULL
  };
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|OO", kwlist,
				   &fname, &names, &weights))
    return NULL;
  
  f=fopen(fname, "r");
  if (!f) {
    PyErr_SetString(PyExc_IOError, strerror(errno));
    return NULL;
  }
  if (igraph_read_graph_lgl(&g, f, PyObject_IsTrue(names), PyObject_IsTrue(weights))) {
    igraphmodule_handle_igraph_error();
    fclose(f);
    return NULL;
  }
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  if (self != NULL) {
    RC_ALLOC("Graph", self);
    igraphmodule_Graph_init_internal(self);
    self->g=g;
  }
  fclose(f);
  
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Reads an edge list from a Pajek file and creates a graph from it.
 * \return the graph
 * \sa igraph_read_graph_pajek
 */
PyObject* igraphmodule_Graph_Read_Pajek(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  igraphmodule_GraphObject *self;
  char* fname=NULL;
  FILE* f;
  igraph_t g;
  
  char *kwlist[] =
  {
    "f", NULL
  };
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &fname))
    return NULL;
  
  f=fopen(fname, "r");
  if (!f) {
    PyErr_SetString(PyExc_IOError, strerror(errno));
    return NULL;
  }
  if (igraph_read_graph_pajek(&g, f)) {
    igraphmodule_handle_igraph_error();
    fclose(f);
    return NULL;
  }
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  if (self != NULL) {
    RC_ALLOC("Graph", self);
    igraphmodule_Graph_init_internal(self);
    self->g=g;
  }
  fclose(f);
  
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Reads a GraphML file and creates a graph from it.
 * \return the graph
 * \sa igraph_read_graph_graphml
 */
PyObject* igraphmodule_Graph_Read_GraphML(PyTypeObject *type,
					  PyObject *args, PyObject *kwds) {
  igraphmodule_GraphObject *self;
  char* fname=NULL;
  FILE* f;
  long int index=0;
  igraph_t g;
  
  char *kwlist[] = {"f", "index", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|i", kwlist,
				   &fname, &index))
     return NULL;

  f=fopen(fname, "r");
  if (!f) {
    PyErr_SetString(PyExc_IOError, strerror(errno));
    return NULL;
  }
  if (igraph_read_graph_graphml(&g, f, index)) {
    igraphmodule_handle_igraph_error();
    fclose(f);
    return NULL;
  }
  self = (igraphmodule_GraphObject*)type->tp_alloc(type, 0);
  if (self != NULL) {
    RC_ALLOC("Graph", self);
    igraphmodule_Graph_init_internal(self);
    self->g=g;
  }
  fclose(f);
   
  return (PyObject*)self;
}

/** \ingroup python_interface_graph
 * \brief Writes the graph as a DIMACS file
 * \return none
 * \sa igraph_write_graph_dimacs
 */
PyObject* igraphmodule_Graph_write_dimacs(igraphmodule_GraphObject *self,
					  PyObject *args,
					  PyObject *kwds)
{
  char* fname=NULL;
  FILE* f;
  long source, target;
  PyObject* capacity_obj=Py_None;
  igraph_vector_t capacity;
  igraph_bool_t capacity_obj_created=0;

  char *kwlist[] =
  {
    "f", "source", "target", "capacity", NULL
  };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "sii|O", kwlist, &fname,
				   &source, &target, &capacity_obj))
     return NULL;
      
  f=fopen(fname, "w");
  if (!f) {
    PyErr_SetString(PyExc_IOError, strerror(errno));
    return NULL;
  }

  if (igraphmodule_PyObject_to_attribute_values(capacity_obj,
						&capacity,
						self, ATTRHASH_IDX_EDGE,
						1.0)) {
    fclose(f);
    return igraphmodule_handle_igraph_error();
  }

  if (capacity_obj == Py_None) {
    capacity_obj_created=1;
    capacity_obj = PyString_FromString("capacity");
  }

  if (igraph_write_graph_dimacs(&self->g, f, source, target, &capacity)) {
    igraphmodule_handle_igraph_error();
    igraph_vector_destroy(&capacity);
    fclose(f);
    if (capacity_obj_created) { Py_DECREF(capacity_obj); }
    return NULL;
  }
  igraph_vector_destroy(&capacity);
  fclose(f);
  if (capacity_obj_created) { Py_DECREF(capacity_obj); }
  
  Py_RETURN_NONE;
}

/** \ingroup python_interface_graph
 * \brief Writes the edge list to a file
 * \return none
 * \sa igraph_write_graph_edgelist
 */
PyObject* igraphmodule_Graph_write_edgelist(igraphmodule_GraphObject *self,
						   PyObject *args,
						   PyObject *kwds)
{
  char* fname=NULL;
  FILE* f;
  
  char *kwlist[] =
  {
    "f", NULL
  };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &fname))
     return NULL;
      
  f=fopen(fname, "w");
  if (!f) {
    PyErr_SetString(PyExc_IOError, strerror(errno));
    return NULL;
  }

  if (igraph_write_graph_edgelist(&self->g, f)) {
    igraphmodule_handle_igraph_error();
    fclose(f);
    return NULL;
  }
  fclose(f);
  
  Py_RETURN_NONE;
}

/** \ingroup python_interface_graph
 * \brief Writes the edge list to a file in .ncol format
 * \return none
 * \sa igraph_write_graph_ncol
 */
PyObject* igraphmodule_Graph_write_ncol(igraphmodule_GraphObject *self,
					       PyObject *args,
					       PyObject *kwds)
{
  char* fname=NULL;
  char* names="name";
  char* weights="weight";
  FILE* f;
  
  char *kwlist[] =
  {
    "f", "names", "weights", NULL
  };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|zz", kwlist,
				   &fname, &names, &weights))
     return NULL;

  f=fopen(fname, "w");
  if (!f) {
    PyErr_SetString(PyExc_IOError, strerror(errno));
    return NULL;
  }
  if (igraph_write_graph_ncol(&self->g, f, names, weights))
  {
    igraphmodule_handle_igraph_error();
    fclose(f);
    return NULL;
  }
  fclose(f);
  
  Py_RETURN_NONE;
}

/** \ingroup python_interface_graph
 * \brief Writes the edge list to a file in .lgl format
 * \return none
 * \sa igraph_write_graph_lgl
 */
PyObject* igraphmodule_Graph_write_lgl(igraphmodule_GraphObject *self,
					      PyObject *args,
					      PyObject *kwds)
{
  char* fname=NULL;
  char* names="name";
  char* weights="weight";
  PyObject* isolates=Py_True;
  FILE* f;
  
  char *kwlist[] =
  {
    "f", "names", "weights", "isolates", NULL
  };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|zzO", kwlist,
				   &fname, &names, &weights, &isolates))
     return NULL;

  f=fopen(fname, "w");
  if (!f) {
    PyErr_SetString(PyExc_IOError, strerror(errno));
    return NULL;
  }
  if (igraph_write_graph_lgl(&self->g, f, names, weights,
			     PyObject_IsTrue(isolates)))
  {
    igraphmodule_handle_igraph_error();
    fclose(f);
    return NULL;
  }
  fclose(f);
  
  Py_RETURN_NONE;
}

/** \ingroup python_interface_graph
 * \brief Writes the graph to a GraphML file
 * \return none
 * \sa igraph_write_graph_graphml
 */
PyObject* igraphmodule_Graph_write_graphml(igraphmodule_GraphObject *self,
					   PyObject *args,
					   PyObject *kwds)
{
  char* fname=NULL;
  FILE* f;
  
  char *kwlist[] =
  {
    "f", NULL
  };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &fname))
     return NULL;
      
  f=fopen(fname, "w");
  if (!f) {
    PyErr_SetString(PyExc_IOError, strerror(errno));
    return NULL;
  }
  if (igraph_write_graph_graphml(&self->g, f))
  {
    igraphmodule_handle_igraph_error();
    fclose(f);
    return NULL;
  }
  fclose(f);
  
  Py_RETURN_NONE;
}

/** \ingroup python_interface_graph
 * \brief Calculates the isomorphy class of a graph or its subgraph
 * \sa igraph_isoclass, igraph_isoclass_subgraph
 */
PyObject* igraphmodule_Graph_isoclass(igraphmodule_GraphObject *self,
				      PyObject* args,
				      PyObject* kwds) {
  int isoclass = 0, n;
  PyObject* vids = 0;
  char *kwlist[] = {"vertices", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O!", kwlist, &PyList_Type, &vids))
     return NULL;
  
  if (vids) {
    /* vertex list was passed, check its length */
    n=PyList_Size(vids);
  } else {
    n=igraph_vcount(&self->g);
  }
  
  if (n < 3 || n > 4) {
    PyErr_SetString(PyExc_ValueError, "Graph or subgraph must have 3 or 4 vertices.");
    return NULL;
  }
  
  if (vids) {
    igraph_vector_t vidsvec;
    if (igraphmodule_PyList_to_vector_t(vids, &vidsvec, 1, 0)) {
      PyErr_SetString(PyExc_ValueError, "Error while converting PyList to igraph_vector_t");
      return NULL;
    }
    if (igraph_isoclass_subgraph(&self->g, &vidsvec, &isoclass)) {
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  } else {
    if (igraph_isoclass(&self->g, &isoclass)) {
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  }
  
  return PyInt_FromLong((long)isoclass);
}

/** \ingroup python_interface_graph
 * \brief Determines whether the graph is isomorphic to another graph
 * \sa igraph_isoclass
 */
PyObject* igraphmodule_Graph_isomorphic(igraphmodule_GraphObject *self, PyObject *args, PyObject *kwds) {
  igraph_bool_t result = 0;
  PyObject *o;
  igraphmodule_GraphObject *other;
  char *kwlist[] = {"other", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist, &igraphmodule_GraphType, &o))
    return NULL;
  other=(igraphmodule_GraphObject*)o;
  
  if (igraph_vcount(&self->g) < 3 || igraph_vcount(&self->g) > 4) {
    PyErr_SetString(PyExc_ValueError, "Graph must have 3 or 4 vertices.");
    return NULL;
  }
  if (igraph_vcount(&other->g) < 3 || igraph_vcount(&other->g) > 4) {
    PyErr_SetString(PyExc_ValueError, "Graph must have 3 or 4 vertices.");
    return NULL;
  }
  
  if (igraph_isomorphic(&self->g, &other->g, &result)) {
    igraphmodule_handle_igraph_error();
    return NULL;
  }
  
  if (result) Py_RETURN_TRUE;
  Py_RETURN_FALSE;
}

/** \ingroup python_interface_graph
 * \brief Returns the number of graph attributes
 */
int igraphmodule_Graph_attribute_count(igraphmodule_GraphObject* self) {
  return PyDict_Size(((PyObject**)self->g.attr)[ATTRHASH_IDX_GRAPH]);
}

/** \ingroup python_interface_graph
 * \brief Returns the corresponding value to a given attribute in the graph
 */
PyObject* igraphmodule_Graph_get_attribute(igraphmodule_GraphObject* self,
					   PyObject* s) {
  PyObject* result;
  
  result=PyDict_GetItem(((PyObject**)self->g.attr)[ATTRHASH_IDX_GRAPH], s);
  if (result) {
    Py_INCREF(result);
    return result;
  }
  
  /* result is NULL, check whether there was an error */
  if (!PyErr_Occurred())
    PyErr_SetString(PyExc_KeyError, "Attribute does not exist");
  return NULL;
}

/** \ingroup python_interface_graph
 * \brief Sets the corresponding value of a given attribute in the graph
 * \param self the graph object
 * \param k the attribute name to be set
 * \param v the value to be set
 * \return 0 if everything's ok, -1 in case of error
 */
int igraphmodule_Graph_set_attribute(igraphmodule_GraphObject* self, PyObject* k, PyObject* v) {
  if (v == NULL)
    return PyDict_DelItem(((PyObject**)self->g.attr)[ATTRHASH_IDX_GRAPH], k);
  Py_INCREF(v);
  if (PyDict_SetItem(((PyObject**)self->g.attr)[ATTRHASH_IDX_GRAPH], k, v) == -1) {
    Py_DECREF(v);
    return -1;
  }
  return 0;
}

/** \ingroup python_interface_graph
 * \brief Returns the attribute list of the graph
 */
PyObject* igraphmodule_Graph_attributes(igraphmodule_GraphObject* self) {
  return PyDict_Keys(((PyObject**)self->g.attr)[ATTRHASH_IDX_GRAPH]);
}

/** \ingroup python_interface_graph
 * \brief Returns the attribute list of the graph's vertices
 */
PyObject* igraphmodule_Graph_vertex_attributes(igraphmodule_GraphObject* self) {
  return PyDict_Keys(((PyObject**)self->g.attr)[ATTRHASH_IDX_VERTEX]);
}

/** \ingroup python_interface_graph
 * \brief Returns the attribute list of the graph's edges
 */
PyObject* igraphmodule_Graph_edge_attributes(igraphmodule_GraphObject* self) {
  return PyDict_Keys(((PyObject**)self->g.attr)[ATTRHASH_IDX_EDGE]);
}

/** \ingroup python_interface_graph
 * \brief Returns the vertex sequence of the graph
 */
PyObject* igraphmodule_Graph_get_vertices(igraphmodule_GraphObject* self, void* closure) {
  if (self->vseq==NULL) {
    self->vseq=igraphmodule_VertexSeq_New(self);
  }
  Py_INCREF(self->vseq);
  return self->vseq;
}

/** \ingroup python_interface_graph
 * \brief Returns the edge sequence of the graph
 */
PyObject* igraphmodule_Graph_get_edges(igraphmodule_GraphObject* self, void* closure) {
  if (self->eseq==NULL) {
    self->eseq=igraphmodule_EdgeSeq_New(self);
  }
  Py_INCREF(self->eseq);
  return self->eseq;
}

/** \ingroup python_interface_graph
 * \brief Creates the disjoint union of two graphs (operator version)
 */
PyObject* igraphmodule_Graph_disjoint_union(igraphmodule_GraphObject* self, PyObject* other) {
  PyObject *it;
  igraphmodule_GraphObject *o, *result;
  igraph_t g;
  
  /* Did we receive an iterable? */
  it=PyObject_GetIter(other);
  if (it) {
    /* Get all elements, store the graphs in an igraph_vector_ptr */
    igraph_vector_ptr_t gs;
    if (igraphmodule_PyIter_to_vector_ptr_t(it, &gs)) {
      Py_DECREF(it);
      return NULL;
    }
    Py_DECREF(it);
    
    /* Create disjoint union */
    if (igraph_disjoint_union_many(&g, &gs)) {
      igraph_vector_ptr_destroy(&gs);
      igraphmodule_handle_igraph_error();
      return NULL;
    }
    
    igraph_vector_ptr_destroy(&gs);
  } else {
    PyErr_Clear();
    if (!PyObject_TypeCheck(other, &igraphmodule_GraphType)) {
      Py_INCREF(Py_NotImplemented);
      return Py_NotImplemented;
    }
    o=(igraphmodule_GraphObject*)other;
  
    if (igraph_disjoint_union(&g, &self->g, &o->g)) {
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  }
  
  result = (igraphmodule_GraphObject*)self->ob_type->tp_alloc(self->ob_type, 0);
  RC_ALLOC("Graph", result);
  if (result != NULL) {
    /* this is correct as long as attributes are not copied by the
     * operator. if they are copied, the initialization should not empty
     * the attribute hashes */
    igraphmodule_Graph_init_internal(result);
    result->g=g;
  }
  
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Creates the union of two graphs (operator version)
 */
PyObject* igraphmodule_Graph_union(igraphmodule_GraphObject* self, PyObject* other) {
  PyObject *it;
  igraphmodule_GraphObject *o, *result;
  igraph_t g;
  
  /* Did we receive an iterable? */
  it=PyObject_GetIter(other);
  if (it) {
    /* Get all elements, store the graphs in an igraph_vector_ptr */
    igraph_vector_ptr_t gs;
    if (igraphmodule_PyIter_to_vector_ptr_t(it, &gs)) {
      Py_DECREF(it);
      return NULL;
    }
    Py_DECREF(it);
    
    /* Create union */
    if (igraph_union_many(&g, &gs)) {
      igraph_vector_ptr_destroy(&gs);
      igraphmodule_handle_igraph_error();
      return NULL;
    }
    
    igraph_vector_ptr_destroy(&gs);
  } else {
    PyErr_Clear();
    if (!PyObject_TypeCheck(other, &igraphmodule_GraphType)) {
      Py_INCREF(Py_NotImplemented);
      return Py_NotImplemented;
    }
    o=(igraphmodule_GraphObject*)other;
  
    if (igraph_union(&g, &self->g, &o->g)) {
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  }
  
  result = (igraphmodule_GraphObject*)self->ob_type->tp_alloc(self->ob_type, 0);
  RC_ALLOC("Graph", result);
  if (result != NULL) {
    /* this is correct as long as attributes are not copied by the
     * operator. if they are copied, the initialization should not empty
     * the attribute hashes */
    igraphmodule_Graph_init_internal(result);
    result->g=g;
  }
  
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Creates the intersection of two graphs (operator version)
 */
PyObject* igraphmodule_Graph_intersection(igraphmodule_GraphObject* self, PyObject* other) {
  PyObject *it;
  igraphmodule_GraphObject *o, *result;
  igraph_t g;
  
  /* Did we receive an iterable? */
  it=PyObject_GetIter(other);
  if (it) {
    /* Get all elements, store the graphs in an igraph_vector_ptr */
    igraph_vector_ptr_t gs;
    if (igraphmodule_PyIter_to_vector_ptr_t(it, &gs)) {
      Py_DECREF(it);
      return NULL;
    }
    Py_DECREF(it);
    
    /* Create union */
    if (igraph_intersection_many(&g, &gs)) {
      igraph_vector_ptr_destroy(&gs);
      igraphmodule_handle_igraph_error();
      return NULL;
    }
    
    igraph_vector_ptr_destroy(&gs);
  } else {
    PyErr_Clear();
    if (!PyObject_TypeCheck(other, &igraphmodule_GraphType)) {
      Py_INCREF(Py_NotImplemented);
      return Py_NotImplemented;
    }
    o=(igraphmodule_GraphObject*)other;
  
    if (igraph_intersection(&g, &self->g, &o->g)) {
      igraphmodule_handle_igraph_error();
      return NULL;
    }
  }
  
  result = (igraphmodule_GraphObject*)self->ob_type->tp_alloc(self->ob_type, 0);
  RC_ALLOC("Graph", result);
  if (result != NULL) {
    /* this is correct as long as attributes are not copied by the
     * operator. if they are copied, the initialization should not empty
     * the attribute hashes */
    igraphmodule_Graph_init_internal(result);
    result->g=g;
  }
  
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Creates the difference of two graphs (operator version)
 */
PyObject* igraphmodule_Graph_difference(igraphmodule_GraphObject* self, PyObject* other) {
  igraphmodule_GraphObject *o, *result;
  igraph_t g;
  
  if (!PyObject_TypeCheck(other, &igraphmodule_GraphType)) {
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
  }
  o=(igraphmodule_GraphObject*)other;
  
  if (igraph_difference(&g, &self->g, &o->g)) {
    igraphmodule_handle_igraph_error();
    return NULL;
  }
  
  result = (igraphmodule_GraphObject*)self->ob_type->tp_alloc(self->ob_type, 0);
  RC_ALLOC("Graph", result);
  if (result != NULL) {
    /* this is correct as long as attributes are not copied by the
     * operator. if they are copied, the initialization should not empty
     * the attribute hashes */
    igraphmodule_Graph_init_internal(result);
    result->g=g;
  }
  
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Creates the complementer of a graph
 */
PyObject* igraphmodule_Graph_complementer(igraphmodule_GraphObject* self, PyObject* args) {
  igraphmodule_GraphObject *result;
  PyObject *o = Py_True;
  igraph_t g;
  
  if (!PyArg_ParseTuple(args, "|O", &o)) return NULL;
  if (igraph_complementer(&g, &self->g, PyObject_IsTrue(o))) {
    igraphmodule_handle_igraph_error();
    return NULL;
  }
  
  result = (igraphmodule_GraphObject*)self->ob_type->tp_alloc(self->ob_type, 0);
  RC_ALLOC("Graph", result);
  if (result != NULL) {
    /* this is correct as long as attributes are not copied by the
     * operator. if they are copied, the initialization should not empty
     * the attribute hashes */
    igraphmodule_Graph_init_internal(result);
    result->g=g;
  }
  
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Creates the complementer of a graph (operator version)
 */
PyObject* igraphmodule_Graph_complementer_op(igraphmodule_GraphObject* self) {
  igraphmodule_GraphObject *result;
  igraph_t g;
  
  if (igraph_complementer(&g, &self->g, 0)) {
    igraphmodule_handle_igraph_error();
    return NULL;
  }
  
  result = (igraphmodule_GraphObject*)self->ob_type->tp_alloc(self->ob_type, 0);
  RC_ALLOC("Graph", result);
  if (result != NULL) {
    /* this is correct as long as attributes are not copied by the
     * operator. if they are copied, the initialization should not empty
     * the attribute hashes */
    igraphmodule_Graph_init_internal(result);
    result->g=g;
  }
  
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Creates the composition of two graphs
 */
PyObject* igraphmodule_Graph_compose(igraphmodule_GraphObject* self, PyObject* other) {
  igraphmodule_GraphObject *o, *result;
  igraph_t g;
  
  if (!PyObject_TypeCheck(other, &igraphmodule_GraphType)) {
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
  }
  o=(igraphmodule_GraphObject*)other;
  
  if (igraph_compose(&g, &self->g, &o->g)) {
    igraphmodule_handle_igraph_error();
    return NULL;
  }
  
  result = (igraphmodule_GraphObject*)self->ob_type->tp_alloc(self->ob_type, 0);
  RC_ALLOC("Graph", result);
  if (result != NULL) {
    /* this is correct as long as attributes are not copied by the
     * operator. if they are copied, the initialization should not empty
     * the attribute hashes */
    igraphmodule_Graph_init_internal(result);
    result->g=g;
  }
  
  return (PyObject*)result;
}

/** \ingroup python_interface_graph
 * \brief Conducts a breadth first search (BFS) on the graph
 */
PyObject* igraphmodule_Graph_bfs(igraphmodule_GraphObject* self, PyObject* args, PyObject* kwds) {
  char *kwlist[] = {"vid", "mode", NULL};
  long vid;
  PyObject *l1, *l2, *l3, *result;
  igraph_neimode_t mode = IGRAPH_OUT;
  igraph_vector_t vids;
  igraph_vector_t layers;
  igraph_vector_t parents;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|i", kwlist, &vid, &mode))
    return NULL;
  if (vid < 0 || vid>igraph_vcount(&self->g)) {
    PyErr_SetString(PyExc_ValueError, "invalid vertex id");
    return NULL;
  }
  
  if (igraph_vector_init(&vids, igraph_vcount(&self->g))) {
    PyErr_SetString(PyExc_MemoryError, "not enough memory");
  }
  if (igraph_vector_init(&layers, igraph_vcount(&self->g))) {
    PyErr_SetString(PyExc_MemoryError, "not enough memory");
  }
  if (igraph_vector_init(&parents, igraph_vcount(&self->g))) {
    PyErr_SetString(PyExc_MemoryError, "not enough memory");
  }
  if (igraph_bfs(&self->g, (igraph_integer_t)vid, mode, &vids, &layers, &parents)) {
    igraphmodule_handle_igraph_error();
    return NULL;
  }
  l1=igraphmodule_vector_t_to_PyList(&vids);
  l2=igraphmodule_vector_t_to_PyList(&layers);
  l3=igraphmodule_vector_t_to_PyList(&parents);
  if (l1 && l2 && l3)
    result=Py_BuildValue("(OOO)", l1, l2, l3);
  else result=NULL;
  igraph_vector_destroy(&vids); igraph_vector_destroy(&layers);
  igraph_vector_destroy(&parents); return result;
}

/** \ingroup python_interface_graph
 * \brief Constructs a breadth first search (BFS) iterator of the graph
 */
PyObject* igraphmodule_Graph_bfsiter(igraphmodule_GraphObject* self, PyObject* args, PyObject* kwds) {
  char *kwlist[] = {"vid", "mode", "advanced", NULL};
  PyObject *root, *adv = Py_False;
  igraph_neimode_t mode = IGRAPH_OUT;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|iO", kwlist, &root, &mode, &adv))
    return NULL;
  
  return igraphmodule_BFSIter_new(self, root, mode, PyObject_IsTrue(adv));
}

/** \ingroup python_interface_graph
 * \brief Calculates the value of the maximum flow in the graph
 */
PyObject* igraphmodule_Graph_maxflow_value(igraphmodule_GraphObject* self,
					   PyObject* args, PyObject* kwds) {
  static char* kwlist[] = {"source", "target", "capacity", NULL};
  PyObject *capacity_object = Py_None;
  igraph_vector_t capacity_vector;
  igraph_real_t result;
  long vid1=-1, vid2=-1;
  igraph_integer_t v1, v2;

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii|O", kwlist,
				   &vid1, &vid2, &capacity_object))
    return NULL;

  v1=vid1; v2=vid2;
  if (igraphmodule_PyObject_to_attribute_values(capacity_object,
						&capacity_vector,
						self, ATTRHASH_IDX_EDGE,
						1.0))
    return igraphmodule_handle_igraph_error();

  
  if (igraph_maxflow_value(&self->g, &result, v1, v2, &capacity_vector)) {
    igraph_vector_destroy(&capacity_vector);
    return igraphmodule_handle_igraph_error();
  }

  igraph_vector_destroy(&capacity_vector);
  return Py_BuildValue("d", (double)result);
}

/** \ingroup python_interface_graph
 * \brief Calculates the value of the minimum cut in the graph
 */
PyObject* igraphmodule_Graph_mincut_value(igraphmodule_GraphObject* self,
					  PyObject* args, PyObject* kwds) {
  static char* kwlist[] = {"source", "target", "capacity", NULL};
  PyObject *capacity_object = Py_None;
  igraph_vector_t capacity_vector;
  igraph_real_t result, mincut;
  igraph_integer_t v1, v2;
  long vid1=-1, vid2=-1;
  long n;

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|iiO", kwlist,
				   &vid1, &vid2, &capacity_object))
    return NULL;

  if (igraphmodule_PyObject_to_attribute_values(capacity_object,
						&capacity_vector,
						self, ATTRHASH_IDX_EDGE,
						1.0))
    return igraphmodule_handle_igraph_error();

  v1=vid1; v2=vid2;
  if (v1 == -1 && v2 == -1) {
    if (igraph_mincut_value(&self->g, &result, &capacity_vector)) {
      igraph_vector_destroy(&capacity_vector);
      return igraphmodule_handle_igraph_error();
    }
  } else if (v1 == -1) {
    n=igraph_vcount(&self->g);
    result=-1;
    for (v1=0; v1<n; v1++) {
      if (v2 == v1) continue;
      if (igraph_st_mincut_value(&self->g, &mincut, v1, v2, &capacity_vector)) {
	igraph_vector_destroy(&capacity_vector);
	return igraphmodule_handle_igraph_error();
      }
      if (result<0 || result>mincut) result=mincut;
    }
    if (result<0) result=0.0;
  } else if (v2 == -1) {
    n=igraph_vcount(&self->g);
    result=-1;
    for (v2=0; v2<n; v2++) {
      if (v2 == v1) continue;
      if (igraph_st_mincut_value(&self->g, &mincut, v1, v2, &capacity_vector)) {
	igraph_vector_destroy(&capacity_vector);
	return igraphmodule_handle_igraph_error();
      }
      if (result<0.0 || result>mincut) result=mincut;
    }
    if (result<0) result=0.0;
  } else {
    if (igraph_st_mincut_value(&self->g, &result, v1, v2, &capacity_vector)) {
      igraph_vector_destroy(&capacity_vector);
      return igraphmodule_handle_igraph_error();
    }
  }

  igraph_vector_destroy(&capacity_vector);
  return Py_BuildValue("d", (double)result);
}

/** \defgroup python_interface_internal Internal functions
 * \ingroup python_interface */

/** \ingroup python_interface_internal
 * \brief Returns the encapsulated igraph graph as a PyCObject
 * \return a new PyCObject
 */
PyObject* igraphmodule_Graph___graph_as_cobject__(igraphmodule_GraphObject *self,
						 PyObject *args,
						 PyObject *kwds) 
{
  /*
  char *kwlist[] = {"ref", NULL};
  PyObject *incref=Py_True;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &incref)) return NULL;
  if (PyObject_IsTrue(incref)) Py_INCREF(self);
  */
  
  return PyCObject_FromVoidPtr((void*)&self->g, NULL);
}


/** \ingroup python_interface_internal
 * \brief Registers a destructor to be called when the object is destroyed
 * \return the previous destructor (if any)
 * Unimplemented.
 */
PyObject* igraphmodule_Graph___register_destructor__(igraphmodule_GraphObject *self,
							    PyObject *args,
							    PyObject *kwds) 
{
  char *kwlist[] = {"destructor", NULL};
  PyObject *destructor = NULL, *result;
  
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O", kwlist, &destructor)) return NULL;
  
  if (!PyCallable_Check(destructor)) {
    PyErr_SetString(PyExc_TypeError, "The destructor must be callable!");
    return NULL;
  }
  
  result=self->destructor;
  self->destructor=destructor;
  Py_INCREF(self->destructor);

  if (!result) Py_RETURN_NONE;
  
  return result;
}

/** \ingroup python_interface
 * \brief Member list of the \c igraph.Graph object type
 */
#define OFF(x) offsetof(igraphmodule_GraphObject, x)

PyGetSetDef igraphmodule_Graph_getseters[] = {
  {"vs", (getter)igraphmodule_Graph_get_vertices, NULL,
      "The sequence of vertices in the graph.", NULL
  },
  {"es", (getter)igraphmodule_Graph_get_edges, NULL,
      "The sequence of edges in the graph.", NULL
  },
  {NULL}
};
/*
static PyMemberDef igraphmodule_Graph_members[] = {
  {"vertices", T_OBJECT, OFF(vseq), RO,
      "Sequence of vertices in the graph"
  },
  {"vs", T_OBJECT, OFF(vseq), RO,
      "Sequence of vertices in the graph. Alias for 'vertices'."
  },
  {"nodes", T_OBJECT, OFF(vseq), RO,
      "Sequence of vertices in the graph. Alias for 'nodes'."
  },
  {NULL}
};*/

/** \ingroup python_interface
 * \brief Method list of the \c igraph.Graph object type
 */
PyMethodDef igraphmodule_Graph_methods[] = 
{
  ////////////////////////////
  // BASIC IGRAPH INTERFACE //
  ////////////////////////////
  
  // interface to igraph_vcount
  {"vcount", (PyCFunction)igraphmodule_Graph_vcount,
      METH_NOARGS,
      "vcount()\n\n"
      "Counts the number of vertices.\n"
      "@return: the number of vertices in the graph.\n"
      "@rtype: integer"
  },
   
  // interface to igraph_ecount
  {"ecount", (PyCFunction)igraphmodule_Graph_ecount,
      METH_NOARGS,
      "ecount()\n\n"
      "Counts the number of edges.\n"
      "@return: the number of edges in the graph.\n"
      "@rtype: integer"
  },
   
  // interface to igraph_is_directed
  {"is_directed", (PyCFunction)igraphmodule_Graph_is_directed,
      METH_NOARGS,
      "is_directed()\n\n"
      "Checks whether the graph is directed."
      "@return: C{True} if it is directed, C{False} otherwise.\n"
      "@rtype: boolean"
  },
   
  // interface to igraph_add_vertices
  {"add_vertices", (PyCFunction)igraphmodule_Graph_add_vertices,
      METH_VARARGS,
      "add_vertices(n)\n\n"
      "Adds vertices to the graph.\n\n"
      "@param n: the number of vertices to be added\n"
      "@return: the same graph object\n"
  },
   
  // interface to igraph_delete_vertices
  {"delete_vertices", (PyCFunction)igraphmodule_Graph_delete_vertices,
      METH_VARARGS,
      "delete_vertices(vs)\n\n"
      "Deletes vertices and all its edges from the graph.\n\n"
      "@param vs: a single vertex ID or the list of vertex IDs\n"
      "  to be deleted.\n"
      "@return: the same graph object\n"
  },
   
  // interface to igraph_add_edges
  {"add_edges", (PyCFunction)igraphmodule_Graph_add_edges,
      METH_VARARGS,
      "add_edges(es)\n\n"
      "Adds edges to the graph.\n\n"
      "@param es: the list of edges to be added. Every edge is\n"
      "  represented with a tuple, containing the vertex IDs of the\n"
      "  two endpoints. Vertices are enumerated from zero. It is\n"
      "  allowed to provide a single pair instead of a list consisting\n"
      "  of only one pair.\n"
      "@return: the same graph object\n"
  },
   
  // interface to igraph_delete_edges
  {"delete_edges", (PyCFunction)igraphmodule_Graph_delete_edges,
      METH_VARARGS | METH_KEYWORDS,
   "delete_edges(es, by_index=False)\n\n"
   "Removes edges from the graph.\n\n"
   "All vertices will be kept, even if they lose all their edges.\n"
   "Nonexistent edges will be silently ignored.\n\n"
   "@param es: the list of edges to be removed.\n"
   "@param by_index: determines how edges are identified. If C{by_index} is\n"
   "  C{False}, every edge is represented with a tuple, containing the\n"
   "  vertex IDs of the two endpoints. Vertices are enumerated from zero.\n"
   "  It is allowed to provide a single pair instead of a list consisting\n"
   "  of only one pair. If C{by_index} is C{True}, edges are identified by\n"
   "  their IDs starting from zero.\n"
   "@return: the same graph object\n"
  },
   
  // interface to igraph_degree
  {"degree", (PyCFunction)igraphmodule_Graph_degree,
      METH_VARARGS | METH_KEYWORDS,
      "degree(vertices, type=ALL, loops=False)\n\n"
      "Returns some vertex degrees from the graph.\n\n"
      "This method accepts a single vertex ID or a list of vertex IDs as a\n"
      "parameter, and returns the degree of the given vertices (in the\n"
      "form of a single integer or a list, depending on the input\n"
      "parameter).\n"
      "\n"
      "@param vertices: a single vertex ID or a list of vertex IDs\n"
      "@param type: the type of degree to be returned (L{OUT} for\n"
      "  out-degrees, L{IN} IN for in-degrees or L{ALL} for the sum of\n"
      "  them).\n"
      "@param loops: whether self-loops should be counted.\n"
  },
   
  // interfaces to igraph_neighbors
  {"neighbors", (PyCFunction)igraphmodule_Graph_neighbors,
      METH_VARARGS | METH_KEYWORDS,
      "neighbors(vertex, type=ALL)\n\n"
      "Returns adjacent vertices to a given vertex.\n\n"
      "@param vertex: a vertex ID\n"
      "@param type: whether to return only predecessors (L{OUT}),\n"
      "  successors (L{OUT}) or both (L{ALL}). Ignored for undirected\n"
      "  graphs."
  },
   
  {"successors", (PyCFunction)igraphmodule_Graph_successors,
      METH_VARARGS | METH_KEYWORDS,
      "successors(vertex)\n\n"
      "Returns the successors of a given vertex.\n\n"
      "Equivalent to calling the L{Graph.neighbors} method with type=L{OUT}."
  },
   
  {"predecessors", (PyCFunction)igraphmodule_Graph_predecessors,
      METH_VARARGS | METH_KEYWORDS,
      "predecessors(vertex)\n\n"
      "Returns the predecessors of a given vertex.\n\n"
      "Equivalent to calling the L{Graph.neighbors} method with type=L{IN}."
  },

  /* interface to igraph_get_eid */
  {"get_eid", (PyCFunction)igraphmodule_Graph_get_eid,
   METH_VARARGS | METH_KEYWORDS,
   "get_eid(v1, v2)\n\n"
   "Returns the edge ID of an arbitrary edge between vertices v1 and v2\n\n"
   "@param v1: the first vertex ID\n"
   "@param v2: the second vertex ID\n"
   "@return: the edge ID of an arbitrary edge between vertices v1 and v2\n"
  },

  //////////////////////
  // GRAPH GENERATORS //
  //////////////////////

  // interface to igraph_adjacency
  {"Adjacency", (PyCFunction)igraphmodule_Graph_Adjacency,
      METH_CLASS | METH_VARARGS | METH_KEYWORDS,
      "Adjacency(matrix, mode=ADJ_DIRECTED)\n\n"
      "Generates a graph from its adjacency matrix.\n\n"
      "@param matrix: the adjacency matrix\n"
      "@param mode: the mode to be used. Possible values are:\n"
      "\n"
      "  - C{ADJ_DIRECTED} - the graph will be directed and a matrix\n"
      "    element gives the number of edges between two vertex.\n"
      "  - C{ADJ_UNDIRECTED} - alias to C{ADJ_MAX} for convenience.\n"
      "  - C{ADJ_MAX}   - undirected graph will be created and the number of\n"
      "    edges between vertex M{i} and M{j} is M{max(A(i,j), A(j,i))}\n"
      "  - C{ADJ_MIN}   - like C{ADJ_MAX}, but with M{min(A(i,j), A(j,i))}\n"
      "  - C{ADJ_PLUS}  - like C{ADJ_MAX}, but with M{A(i,j) + A(j,i)}\n"
      "  - C{ADJ_UPPER} - undirected graph with the upper right triangle of\n"
      "    the matrix (including the diagonal)\n"
      "  - C{ADJ_LOWER} - undirected graph with the lower left triangle of\n"
      "    the matrix (including the diagonal)\n"
      " Optional, defaults to ADJ_DIRECTED.\n"
  },
  
  /* interface to igraph_asymmetric_preference_game */
  {"Asymmetric_Preference", (PyCFunction)igraphmodule_Graph_Asymmetric_Preference,
   METH_VARARGS | METH_CLASS | METH_KEYWORDS,
   "Asymmetric_Preference(n, type_dist_matrix, pref_matrix, attribute=None, loops=False)\n\n"
   "Generates a graph based on asymmetric vertex types and connection probabilities.\n\n"
   "This is the asymmetric variant of L{Graph.Preference}.\n"
   "A given number of vertices are generated. Every vertex is assigned to an\n"
   "\"incoming\" and an \"outgoing\" vertex typeaccording to the given joint\n"
   "type probabilities. Finally, every vertex pair is evaluated and a\n"
   "directed edge is created between them with a probability depending on\n"
   "the \"outgoing\" type of the source vertex and the \"incoming\" type of\n"
   "the target vertex.\n\n"
   "@param n: the number of vertices in the graph\n"
   "@param type_dist_matrix: matrix giving the joint distribution of vertex\n"
   "  types\n"
   "@param pref_matrix: matrix giving the connection probabilities for\n"
   "  different vertex types.\n"
   "@param attribute: the vertex attribute name used to store the vertex\n"
   "  types. If C{None}, vertex types are not stored.\n"
   "@param loops: whether loop edges are allowed.\n"
  },
  
  // interface to igraph_atlas
  {"Atlas", (PyCFunction)igraphmodule_Graph_Atlas,
      METH_CLASS | METH_KEYWORDS,
      "Atlas(idx)\n\n"
      "Generates a graph from the Graph Atlas.\n\n"
      "@param idx: The index of the graph to be generated.\n"
      "  Indices start from zero, graphs are listed:\n\n"
      "    1. in increasing order of number of nodes;\n"
      "    2. for a fixed number of nodes, in increasing order of the\n"
      "       number of edges;\n"
      "    3. for fixed numbers of nodes and edges, in increasing order\n"
      "       of the degree sequence, for example 111223 < 112222;\n"
      "    4. for fixed degree sequence, in increasing number of automorphisms.\n"
  },
	
  // interface to igraph_barabasi_game
  {"Barabasi", (PyCFunction)igraphmodule_Graph_Barabasi,
      METH_VARARGS | METH_CLASS | METH_KEYWORDS,
      "Barabasi(n, m, outpref=False, directed=False, power=1)\n\n"
      "Generates a graph based on the Barabasi-Albert model.\n\n"
      "@param n: the number of vertices\n"
      "@param m: either the number of outgoing edges generated for\n"
      "  each vertex or a list containing the number of outgoing\n"
      "  edges for each vertex explicitly.\n"
      "@param outpref: C{True} if the out-degree of a given vertex\n"
      "  should also increase its citation probability (as well as\n"
      "  its in-degree), but it defaults to C{False}.\n"
      "@param directed: C{True} if the generated graph should be\n"
      "  directed (default: C{False}).\n"
      "@param power: the power constant of the nonlinear model.\n"
      "  It can be omitted, and in this case the usual linear model\n"
      "  will be used.\n"
  },

  // interface to igraph_establishment_game
  {"Establishment", (PyCFunction)igraphmodule_Graph_Establishment,
      METH_VARARGS | METH_CLASS | METH_KEYWORDS,
      "Establishment(n, k, type_dist, pref_matrix, directed=False)\n\n"
      "Generates a graph based on a simple growing model with vertex types.\n\n"
      "A single vertex is added at each time step. This new vertex tries to\n"
      "connect to k vertices in the graph. The probability that such a\n"
      "connection is realized depends on the types of the vertices involved.\n"
      "\n"
      "@param n: the number of vertices in the graph\n"
      "@param k: the number of connections tried in each step\n"
      "@param type_dist: list giving the distribution of vertex types\n"
      "@param pref_matrix: matrix (list of lists) giving the connection\n"
      "  probabilities for different vertex types\n"
      "@param directed: whether to generate a directed graph.\n"
  },
  
  // interface to igraph_erdos_renyi_game
  {"Erdos_Renyi", (PyCFunction)igraphmodule_Graph_Erdos_Renyi,
      METH_VARARGS | METH_CLASS | METH_KEYWORDS,
      "Erdos_Renyi(n, p, m, directed=False, loops=False)\n\n"
      "Generates a graph based on the Erdos-Renyi model.\n\n"
      "@param n: the number of vertices.\n"
      "@param p: the probability of edges. If given, C{m} must be missing.\n"
      "@param m: the number of edges. If given, C{p} must be missing.\n"
      "@param directed: whether to generate a directed graph.\n"
      "@param loops: whether self-loops are allowed.\n"
  },
  
  /* interface to igraph_full */
  {"Full", (PyCFunction)igraphmodule_Graph_Full,
      METH_VARARGS | METH_CLASS | METH_KEYWORDS,
      "Full(n, directed=False, loops=False)\n\n"
      "Generates a full graph (directed or undirected, with or without loops).\n\n"
      "@param n: the number of vertices.\n"
      "@param directed: whether to generate a directed graph.\n"
      "@param loops: whether self-loops are allowed.\n"
  },
  
  /* interface to igraph_grg_game */
  {"GRG", (PyCFunction)igraphmodule_Graph_GRG,
   METH_VARARGS | METH_CLASS | METH_KEYWORDS,
   "GRG(n, radius, torus=False)\n\n"
   "Generates a growing random geometric graph.\n\n"
   "The algorithm drops the vertices randomly on the 2D unit square and connects\n"
   "them if they are closer to each other than the given radius.\n\n"
   "@param n: The number of vertices in the graph\n"
   "@param radius: The given radius\n"
   "@param torus: This should be C{True} if we want to use a torus instead of a\n"
   "  square."
  },
  
  // interface to igraph_growing_random_game
  {"Growing_Random", (PyCFunction)igraphmodule_Graph_Growing_Random,
      METH_VARARGS | METH_CLASS | METH_KEYWORDS,
      "Growing_Random(n, m, directed=False, citation=False)\n\n"
      "Generates a growing random graph.\n\n"
      "@param n: The number of vertices in the graph\n"
      "@param m: The number of edges to add in each step (after adding a new vertex)\n"
      "@param directed: whether the graph should be directed.\n"
      "@param citation: whether the new edges should originate from the most\n"
      "   recently added vertex.\n"
  },
  
  /* interface to igraph_preference_game */
  {"Preference", (PyCFunction)igraphmodule_Graph_Preference,
   METH_VARARGS | METH_CLASS | METH_KEYWORDS,
   "Preference(n, type_dist, pref_matrix, attribute=None, directed=False, loops=False)\n\n"
   "Generates a graph based on vertex types and connection probabilities.\n\n"
   "This is practically the nongrowing variant of L{Graph.Establishment}.\n"
   "A given number of vertices are generated. Every vertex is assigned to a\n"
   "vertex type according to the given type probabilities. Finally, every\n"
   "vertex pair is evaluated and an edge is created between them with a\n"
   "probability depending on the types of the vertices involved.\n\n"
   "@param n: the number of vertices in the graph\n"
   "@param type_dist: list giving the distribution of vertex types\n"
   "@param pref_matrix: matrix giving the connection probabilities for\n"
   "  different vertex types.\n"
   "@param attribute: the vertex attribute name used to store the vertex\n"
   "  types. If C{None}, vertex types are not stored.\n"
   "@param directed: whether to generate a directed graph.\n"
   "@param loops: whether loop edges are allowed.\n"
  },
  
  /* interface to igraph_recent_degree_game */
  {"Recent_Degree", (PyCFunction)igraphmodule_Graph_Recent_Degree,
   METH_VARARGS | METH_CLASS | METH_KEYWORDS,
   "Recent_Degree(n, m, window, outpref=False, directed=False, power=1)\n\n"
   "Generates a graph based on a stochastic model where the probability\n"
   "of an edge gaining a new node is proportional to the edges gained in\n"
   "a given time window.\n\n"
   "@param n: the number of vertices\n"
   "@param m: either the number of outgoing edges generated for\n"
   "  each vertex or a list containing the number of outgoing\n"
   "  edges for each vertex explicitly.\n"
   "@param window: size of the window in time steps\n"
   "@param outpref: C{True} if the out-degree of a given vertex\n"
   "  should also increase its citation probability (as well as\n"
   "  its in-degree), but it defaults to C{False}.\n"
   "@param directed: C{True} if the generated graph should be\n"
   "  directed (default: C{False}).\n"
   "@param power: the power constant of the nonlinear model.\n"
   "  It can be omitted, and in this case the usual linear model\n"
   "  will be used.\n"
  },

  // interface to igraph_star
  {"Star", (PyCFunction)igraphmodule_Graph_Star,
      METH_VARARGS | METH_CLASS | METH_KEYWORDS,
      "Star(n, mode=STAR_UNDIRECTED, center=0)\n\n"
      "Generates a star graph.\n\n"
      "@param n: the number of vertices in the graph\n"
      "@param mode: Gives the type of the star graph to create. Should be\n"
      "  one of the constants C{STAR_OUT}, C{STAR_IN} and C{STAR_UNDIRECTED}.\n"
      "@param center: Vertex ID for the central vertex in the star.\n"
  },
  
  // interface to igraph_lattice
  {"Lattice", (PyCFunction)igraphmodule_Graph_Lattice,
      METH_VARARGS | METH_CLASS | METH_KEYWORDS,
      "Lattice(dim, nei=1, directed=False, mutual=True, circular=True)\n\n"
      "Generates a regular lattice.\n\n"
      "@param dim: list with the dimensions of the lattice\n"
      "@param nei: value giving the distance (number of steps) within which\n"
      "   two vertices will be connected. Not implemented yet.\n"
      "@param directed: whether to create a directed graph.\n"
      "@param mutual: whether to create all connections as mutual\n"
      "    in case of a directed graph.\n"
      "@param circular: whether the generated lattice is periodic.\n"
  },
  
  // interface to igraph_ring
  {"Ring", (PyCFunction)igraphmodule_Graph_Ring,
      METH_VARARGS | METH_CLASS | METH_KEYWORDS,
      "Ring(n, directed=False, mutual=False, circular=True)\n\n"
      "Generates a ring graph.\n\n"
      "@param n: the number of vertices in the ring\n"
      "@param directed: whether to create a directed ring.\n"
      "@param mutual: whether to create mutual edges in a directed ring.\n"
      "@param circular: whether to create a closed ring.\n"
  },
  
  // interface to igraph_tree
  {"Tree", (PyCFunction)igraphmodule_Graph_Tree,
      METH_VARARGS | METH_CLASS | METH_KEYWORDS,
      "Tree(n, children, type=TREE_UNDIRECTED)\n\n"
      "Generates a tree in which almost all vertices have the same number of children.\n\n"
      "@param n: the number of vertices in the graph\n"
      "@param children: the number of children of a vertex in the graph\n"
      "@param type: determines whether the tree should be directed, and if\n"
      "  this is the case, also its orientation. Must be one of\n"
      "  C{TREE_IN}, C{TREE_OUT} and C{TREE_UNDIRECTED}.\n"
  },
  
  // interface to igraph_degree_sequence_game
  {"Degree_Sequence", (PyCFunction)igraphmodule_Graph_Degree_Sequence,
      METH_VARARGS | METH_CLASS | METH_KEYWORDS,
      "Degree_Sequence(out, in=None)\n\n"
      "Generates a graph with a given degree sequence.\n\n"
      "@param out: the out-degree sequence for a directed graph. If the\n"
      "  in-degree sequence is omitted, the generated graph\n"
      "  will be undirected, so this will be the in-degree\n"
      "  sequence as well\n"
      "@param in: the in-degree sequence for a directed graph.\n"
      "   If omitted, the generated graph will be undirected.\n"
  },
  
  // interface to igraph_isoclass_create
  {"Isoclass", (PyCFunction)igraphmodule_Graph_Isoclass,
      METH_VARARGS | METH_CLASS | METH_KEYWORDS,
      "Isoclass(n, class, directed=False)\n\n"
      "Generates a graph with a given isomorphy class.\n\n"
      "@param n: the number of vertices in the graph (3 or 4)\n"
      "@param class: the isomorphy class\n"
      "@param directed: whether the graph should be directed.\n"
  },
  
  /////////////////////////////////////
  // STRUCTURAL PROPERTIES OF GRAPHS //
  /////////////////////////////////////
  
  // interface to igraph_are_connected
  {"are_connected", (PyCFunction)igraphmodule_Graph_are_connected,
      METH_VARARGS | METH_KEYWORDS,
      "are_connected(v1, v2)\n\n"
      "Decides whether two given vertices are directly connected.\n\n"
      "@param v1: the first vertex\n"
      "@param v2: the second vertex\n"
      "@return: C{True} if there exists an edge from v1 to v2, C{False}\n"
      "  otherwise.\n"
  },
  
  // interface to igraph_average_path_length
  {"average_path_length", (PyCFunction)igraphmodule_Graph_average_path_length,
      METH_VARARGS | METH_KEYWORDS,
      "average_path_length(directed=True, unconn=True)\n\n"
      "Calculates the average path length in a graph.\n\n"
      "@param directed: whether to consider directed paths in case of a\n"
      "  directed graph. Ignored for undirected graphs.\n"
      "@param unconn: what to do when the graph is unconnected. If C{True},\n"
      "  the average of the geodesic lengths in the components is\n"
      "  calculated. Otherwise for all unconnected vertex pairs,\n"
      "  a path length equal to the number of vertices is used.\n"
      "@return: the average path length in the graph\n"
  },
  
  // interface to igraph_betweenness
  {"betweenness", (PyCFunction)igraphmodule_Graph_betweenness,
      METH_VARARGS | METH_KEYWORDS,
      "betweenness(vertices=None, directed=True)\n\n"
      "Calculates the betweenness of nodes in a graph.\n\n"
      "Keyword arguments:\n"
      "@param vertices: the vertices for which the betweennesses must be returned.\n"
      "  If C{None}, assumes all of the vertices in the graph.\n"
      "@param directed: whether to consider directed paths.\n"
      "@return: the betweenness of the given nodes in a list\n"
  },
  
  // interface to igraph_bibcoupling
  {"bibcoupling", (PyCFunction)igraphmodule_Graph_bibcoupling,
      METH_VARARGS | METH_KEYWORDS,
      "bibcoupling(vertices)\n\n"
      "Calculates bibliographic coupling values for given vertices\n"
      "in a graph.\n\n"
      "@param vertices: the vertices to be analysed.\n"
      "@return: bibliographic coupling values for all given\n"
      "  vertices in a matrix.\n"
  },
  
  // interface to igraph_closeness
  {"closeness", (PyCFunction)igraphmodule_Graph_closeness,
      METH_VARARGS | METH_KEYWORDS,
      "closeness(vertices=None, mode=ALL)\n\n"
      "Calculates the closeness centralities of given nodes in a graph.\n\n"
      "The closeness centerality of a vertex measures how easily other\n"
      "vertices can be reached from it (or the other way: how easily it\n"
      "can be reached from the other vertices). It is defined as the\n"
      "number of the number of vertices minus one divided by the sum of\n"
      "the lengths of all geodesics from/to the given vertex.\n\n"
      "If the graph is not connected, and there is no path between two\n"
      "vertices, the number of vertices is used instead the length of\n"
      "the geodesic. This is always longer than the longest possible\n"
      "geodesic.\n\n"
      "@param vertices: the vertices for which the closenesses must\n"
      "  be returned. If C{None}, uses all of the vertices in the graph.\n"
      "@param mode: must be one of C{IN}, C{OUT} and C{ALL}. C{IN} means\n"
      "  that the length of the incoming paths, C{OUT} means that the\n"
      "  length of the outgoing paths must be calculated. C{ALL} means\n"
      "  that both of them must be calculated.\n"
      "@return: the calculated closenesses in a list\n"
  },   	    
  
  // interface to igraph_clusters
  {"clusters", (PyCFunction)igraphmodule_Graph_clusters,
      METH_VARARGS | METH_KEYWORDS,
      "clusters(mode=STRONG)\n\n"
      "Calculates the (strong or weak) clusters for a given graph.\n\n"
      "@param mode: must be either C{STRONG} or C{WEAK}, depending on\n"
      "  the clusters being sought. Optional, defaults to C{STRONG}.\n"
      "@return: the component index for every node in the graph.\n"
  },
  {"components", (PyCFunction)igraphmodule_Graph_clusters,
      METH_VARARGS | METH_KEYWORDS,
      "components(mode=STRONG)\n\n"
      "Alias for L{Graph.clusters}.\n\n"
      "See the documentation of L{Graph.clusters} for details."
  },
  {"copy", (PyCFunction)igraphmodule_Graph_copy,
      METH_NOARGS,
      "copy()\n\n"
      "Creates an exact deep copy of the graph."
  },
  {"decompose", (PyCFunction)igraphmodule_Graph_decompose,
      METH_VARARGS | METH_KEYWORDS,
      "decompose(mode=STRONG, maxcompno=None, minelements=1)\n\n"
      "Decomposes the graph into subgraphs.\n\n"
      "@param mode: must be either STRONG or WEAK, depending on the\n"
      "  clusters being sought.\n"
      "@param maxcompno: maximum number of components to return.\n"
      "  C{None} means all possible components.\n"
      "@param minelements: minimum number of vertices in a component.\n"
      "  By setting this to 2, isolated vertices are not returned\n"
      "  as separate components.\n"
      "@return: a list of the subgraphs. Every returned subgraph is a\n"
      "  copy of the original.\n"
  },
  
  // interface to igraph_cocitation
  {"cocitation", (PyCFunction)igraphmodule_Graph_cocitation,
      METH_VARARGS | METH_KEYWORDS,
      "cocitation(vertices)\n\n"
      "Calculates cocitation scores for given vertices in a graph.\n\n"
      "@param vertices: the vertices to be analysed.\n"
      "@return: cocitation scores for all given vertices in a matrix."
  },
  
  /* interface to igraph_constraint */
  {"constraint", (PyCFunction)igraphmodule_Graph_constraint,
   METH_VARARGS | METH_KEYWORDS,
   "cocitation(vertices=None, weights=None)\n\n"
   "Calculates Burt's constraint scores for given vertices in a graph.\n\n"
   "Burt's constraint is higher if ego has less, or mutually stronger\n"
   "related (i.e. more redundant) contacts. Burt's measure of\n"
   "constraint, C[i], of vertex i's ego network V[i], is defined for\n"
   "directed and valued graphs as follows:\n\n"
   "C[i] = sum( sum( (p[i,q] p[q,j])^2, q in V[i], q != i,j ), j in V[], j != i)\n\n"
   "for a graph of order (ie. number od vertices) N, where proportional\n"
   "tie strengths are defined as follows:\n\n"
   "p[i,j]=(a[i,j]+a[j,i]) / sum(a[i,k]+a[k,i], k in V[i], k != i),\n"
   "a[i,j] are elements of A and the latter being the graph adjacency matrix.\n\n"
   "For isolated vertices, constraint is undefined.\n\n"
   "@param vertices: the vertices to be analysed or C{None} for all vertices.\n"
   "@param weights: weights associated to the edges. Can be an attribute name\n"
   "  as well. If C{None}, every edge will have the same weight.\n"
   "@return: cocitation scores for all given vertices in a matrix."
  },
  
  /* interface to igraph_density */
  {"density", (PyCFunction)igraphmodule_Graph_density,
      METH_VARARGS | METH_KEYWORDS,
       "density(loops=False)\n\n"
       "Calculates the density of the graph.\n\n"
       "@param loops: whether to take loops into consideration. If C{True},\n"
       "  the algorithm assumes that there might be some loops in the graph\n"
       "  and calculates the density accordingly. If C{False}, the algorithm\n"
       "  assumes that there can't be any loops.\n"
      "@return: the reciprocity of the graph."
  },
  
  // interface to igraph_diameter
  {"diameter", (PyCFunction)igraphmodule_Graph_diameter,
      METH_VARARGS | METH_KEYWORDS,
      "diameter(directed=True, unconn=True)\n\n"
      "Calculates the diameter of the graph.\n\n"
      "@param directed: whether to consider directed paths.\n"
      "@param unconn: if C{True} and the graph is undirected, the\n"
      "  longest geodesic within a component will be returned. If\n"
      "  C{False} and the graph is undirected, the result is the\n"
      "  number of vertices."
  },
  
  // interface to igraph_edge_betweenness
  {"edge_betweenness", (PyCFunction)igraphmodule_Graph_edge_betweenness,
      METH_VARARGS | METH_KEYWORDS,
      "edge_betweenness(directed=True)\n\n"
      "Calculates the edge betweennesses in a graph.\n\n"
      "@param directed: whether to consider directed paths.\n"
      "@return: a list with the edge betweennesses of all specified edges.\n"
  },
  
  // interface to igraph_get_shortest_paths
  {"get_shortest_paths", (PyCFunction)igraphmodule_Graph_get_shortest_paths,
      METH_VARARGS | METH_KEYWORDS,
      "get_shortest_paths(v, mode=OUT)\n\n"
      "Calculates the shortest paths from/to a given node in a graph.\n\n"
      "@param v: the source/destination for the calculated paths\n"
      "@param mode: the directionality of the paths. C{IN} means to\n"
      "  calculate incoming paths, C{OUT} means to calculate outgoing\n"
      "  paths, C{ALL} means to calculate both ones.\n"
      "@return: at most one shortest path for every node in the graph in a\n"
      "list. For unconnected graphs, some of the list elements will be\n"
      "empty lists. Note that in case of mode=C{IN}, the nodes in a path\n"
      "are returned in reversed order!"
  },
  
  // interface to igraph_get_all_shortest_paths
  {"get_all_shortest_paths", (PyCFunction)igraphmodule_Graph_get_all_shortest_paths,
      METH_VARARGS | METH_KEYWORDS,
      "get_all_shortest_paths(v, mode=OUT)\n\n"
      "Calculates all of the shortest paths from/to a given node in a graph.\n\n"
      "@param v: the source/destination for the calculated paths\n"
      "@param mode: the directionality of the paths. C{IN} means to calculate\n"
      "  incoming paths, C{OUT} means to calculate outgoing paths,\n"
      "  C{ALL} means to calculate both ones.\n"
      "@return: all of the shortest path from the given node to every other\n"
      "reachable node in the graph in a list. Note that in case of mode=C{IN},\n"
      "the nodes in a path are returned in reversed order!"
  },
  
  // interface to igraph_is_connected
  {"is_connected", (PyCFunction)igraphmodule_Graph_is_connected,
      METH_VARARGS | METH_KEYWORDS,
      "is_connected(mode=STRONG)\n\n"
      "Decides whether a graph is connected.\n\n"
      "@param mode: whether we should calculate strong or weak connectivity.\n"
      "@return: C{True} if the graph is connected, C{False} otherwise.\n"
  },
  
  /* interface to igraph_maxdegree */
  {"maxdegree", (PyCFunction)igraphmodule_Graph_maxdegree,
      METH_VARARGS | METH_KEYWORDS,
      "maxdegree(vertices=None, type=ALL, loops=False)\n\n"
      "Returns the maximum degree of a vertex set in the graph.\n\n"
      "This method accepts a single vertex ID or a list of vertex IDs as a\n"
      "parameter, and returns the degree of the given vertices (in the\n"
      "form of a single integer or a list, depending on the input\n"
      "parameter).\n"
      "\n"
      "@param vertices: a single vertex ID or a list of vertex IDs or\n"
      "  C{None} meaning all the vertices in the graph.\n"
      "@param type: the type of degree to be returned (L{OUT} for\n"
      "  out-degrees, L{IN} IN for in-degrees or L{ALL} for the sum of\n"
      "  them).\n"
      "@param loops: whether self-loops should be counted.\n"
  },
   
  // interface to igraph_pagerank
  {"pagerank", (PyCFunction)igraphmodule_Graph_pagerank,
      METH_VARARGS | METH_KEYWORDS,
      "pagerank(vertices=None, directed=True, niter=1000, eps=0.001, damping=0.85)\n\n"
      "Calculates the Google PageRank values of a graph.\n\n"
      "@param vertices: the indices of the vertices being queried.\n"
      "  C{None} means all of the vertices.\n"
      "@param directed: whether to consider directed paths.\n"
      "@param niter: the maximum number of iterations to be performed.\n"
      "@param eps: the iteration stops if all of the PageRank values change\n"
      "  less than M{eps} between two iterations.\n"
      "@param damping: the damping factor.\n"
      "  M{1-damping} is the PageRank value for nodes with no\n"
      "  incoming links.\n"
      "@return: a list with the Google PageRank values of the specified\n"
      "  vertices."
  },

  // interface to igraph_reciprocity
  {"reciprocity", (PyCFunction)igraphmodule_Graph_reciprocity,
      METH_VARARGS | METH_KEYWORDS,
      "reciprocity()\n\n"
      "@return: the reciprocity of the graph."
  },
  
  // interface to igraph_rewire
  {"rewire", (PyCFunction)igraphmodule_Graph_rewire,
      METH_VARARGS | METH_KEYWORDS,
      "rewire(n=1000, mode=REWIRING_SIMPLE)\n\n"
      "Randomly rewires the graph while preserving the degree distribution.\n\n"
      "Please note that the rewiring is done \"in-place\", so the original\n"
      "graph will be modified. If you want to preserve the original graph,\n"
      "use the L{copy} method before.\n\n"
      "@param n: the number of rewiring trials.\n"
      "@param mode: the rewiring algorithm to use. As for now, only\n"
      "  C{REWIRING_SIMPLE} is supported.\n"
      "@return: the modified graph.\n"
  },
  
  // interface to igraph_shortest_paths
  {"shortest_paths", (PyCFunction)igraphmodule_Graph_shortest_paths,
      METH_VARARGS | METH_KEYWORDS,
      "shortest_paths(vertices, mode=OUT)\n\n"
      "Calculates shortest path lengths for given nodes in a graph.\n\n"
      "@param vertices: a list containing the vertex IDs which should be\n"
      "  included in the result.\n"
      "@param mode: the type of shortest paths to be used for the\n"
      "  calculation in directed graphs. C{OUT} means only outgoing,\n"
      "  C{IN} means only incoming paths. C{ALL} means to consider\n"
      "  the directed graph as an undirected one.\n"
      "@return: the shortest path lengths for given nodes in a matrix\n"
  },
  
  // interface to igraph_simplify
  {"simplify", (PyCFunction)igraphmodule_Graph_simplify,
      METH_VARARGS | METH_KEYWORDS,
      "simplify(multiple=True, loops=True)\n\n"
      "Simplifies a graph by removing self-loops and/or multiple edges.\n\n"
      "@param multiple: whether to remove multiple edges.\n"
      "@param loops: whether to remove loops.\n"
  },
  
  // interface to igraph_minimum_spanning_tree_unweighted and
  // igraph_minimum_spanning_tree_prim
  {"spanning_tree", (PyCFunction)igraphmodule_Graph_spanning_tree,
      METH_VARARGS | METH_KEYWORDS,
      "spanning_tree(weights=None)\n\n"
      "Calculates a minimum spanning tree for a graph (weighted or unweighted)\n\n"
      "@param weights: a vector containing weights for every edge in\n"
      "  the graph. C{None} means that the graph is unweighted.\n"
      "@return: the spanning tree as an igraph.Graph object."
  },
  
  // interface to igraph_subcomponent
  {"subcomponent", (PyCFunction)igraphmodule_Graph_subcomponent,
      METH_VARARGS | METH_KEYWORDS,
      "subcomponent(v, mode=ALL)\n\n"
      "Determines the indices of vertices which are in the same component as a given vertex.\n\n"
      "@param v: the index of the vertex used as the source/destination\n"
      "@param mode: if equals to C{IN}, returns the vertex IDs from\n"
      "  where the given vertex can be reached. If equals to C{OUT},\n"
      "  returns the vertex IDs which are reachable from the given\n"
      "  vertex. If equals to C{ALL}, returns all vertices within the\n"
      "  same component as the given vertex, ignoring edge directions.\n"
      "  Note that this is not equal to calculating the union of the \n"
      "  results of C{IN} and C{OUT}.\n"
      "@return: the indices of vertices which are in the same component as a given vertex.\n"
  },
  
  // interface to igraph_subgraph
  {"subgraph", (PyCFunction)igraphmodule_Graph_subgraph,
      METH_VARARGS | METH_KEYWORDS,
      "subgraph(vertices)\n\n"
      "Returns a subgraph based on the given vertices.\n\n"
      "@param vertices: a list containing the vertex IDs which\n"
      "  should be included in the result.\n"
      "@return: a copy of the subgraph\n"
  },
  
  // interface to igraph_transitivity_undirected
  {"transitivity_undirected", (PyCFunction)igraphmodule_Graph_transitivity_undirected,
      METH_VARARGS | METH_KEYWORDS,
      "transitivity_undirected()\n\n"
      "Calculates the transitivity (clustering coefficient) of the graph.\n\n"
      "@return: the transitivity\n"
  },
  
  // interface to igraph_transitivity_local_undirected
  {"transitivity_local_undirected", (PyCFunction)igraphmodule_Graph_transitivity_local_undirected,
      METH_VARARGS | METH_KEYWORDS,
   "transitivity_local_undirected(vertices=None)\n\n"
   "Calculates the local transitivity of given vertices in the graph.\n\n"
   "@param vertices: a list containing the vertex IDs which should be\n"
   "  included in the result. C{None} means all of the vertices.\n"
   "@return: the transitivities for the given vertices in a list\n"
  },
  
  //////////////////////
  // LAYOUT FUNCTIONS //
  //////////////////////
  
  // interface to igraph_layout_circle
  {"layout_circle", (PyCFunction)igraphmodule_Graph_layout_circle,
      METH_VARARGS | METH_KEYWORDS,
      "layout_circle()\n\n"
      "Places the vertices of the graph uniformly on a circle.\n\n"
      "@return: the calculated coordinate pairs in a list."
  },
  
  // interface to igraph_layout_sphere
  {"layout_sphere", (PyCFunction)igraphmodule_Graph_layout_sphere,
      METH_VARARGS | METH_KEYWORDS,
      "layout_sphere()\n\n"
      "Places the vertices of the graph uniformly on a sphere.\n\n"
      "@return: the calculated coordinate triplets in a list."
  },
  
  // interface to igraph_layout_kamada_kawai
  {"layout_kamada_kawai", (PyCFunction)igraphmodule_Graph_layout_kamada_kawai,
      METH_VARARGS | METH_KEYWORDS,
      "layout_kamada_kawai(maxiter=1000, sigma=None, initemp=10, coolexp=0.99, kkconst=None)\n\n"
      "Places the vertices on a plane according to the Kamada-Kawai algorithm.\n\n"
      "This is a force directed layout, see Kamada, T. and Kawai, S.:\n"
      "An Algorithm for Drawing General Undirected Graphs.\n"
      "Information Processing Letters, 31/1, 7--15, 1989.\n\n"
      "@param maxiter: the number of iterations to perform.\n"
      "@param sigma: the standard base deviation of the position\n"
      "  change proposals. C{None} means the number of vertices * 0.25\n"
      "@param initemp: initial temperature of the simulated annealing.\n"
      "@param coolexp: cooling exponent of the simulated annealing.\n"
      "@param kkconst: the Kamada-Kawai vertex attraction constant.\n"
      "  C{None} means the square of the number of vertices.\n"
      "@return: the calculated coordinate pairs in a list."
  },
  
  // interface to igraph_layout_kamada_kawai_3d
  {"layout_kamada_kawai_3d", (PyCFunction)igraphmodule_Graph_layout_kamada_kawai_3d,
      METH_VARARGS | METH_KEYWORDS,
      "layout_kamada_kawai_3d(maxiter=1000, sigma=None, initemp=10, coolexp=0.99, kkconst=None)\n\n"
      "Places the vertices in the 3D space according to the Kamada-Kawai algorithm.\n\n"
      "This is a force directed layout, see Kamada, T. and Kawai, S.:\n"
      "An Algorithm for Drawing General Undirected Graphs.\n"
      "Information Processing Letters, 31/1, 7--15, 1989.\n\n"
      "@param maxiter: the number of iterations to perform.\n"
      "@param sigma: the standard base deviation of the position\n"
      "  change proposals. C{None} means the number of vertices * 0.25\n"
      "@param initemp: initial temperature of the simulated annealing.\n"
      "@param coolexp: cooling exponent of the simulated annealing.\n"
      "@param kkconst: the Kamada-Kawai vertex attraction constant.\n"
      "  C{None} means the square of the number of vertices.\n"
      "@return: the calculated coordinate triplets in a list."
  },
  
  // interface to igraph_layout_fruchterman_reingold
  {"layout_fruchterman_reingold", (PyCFunction)igraphmodule_Graph_layout_fruchterman_reingold,
      METH_VARARGS | METH_KEYWORDS,
      "layout_fruchterman_reingold(maxiter=500, maxdelta=None, area=None, coolexp=0.99, repulserad=maxiter*maxdelta)\n\n"
      "Places the vertices on a 2D plane according to the Fruchterman-Reingold algorithm.\n\n"
      "This is a force directed layout, see Fruchterman, T. M. J. and Reingold, E. M.:\n"
      "Graph Drawing by Force-directed Placement.\n"
      "Software -- Practice and Experience, 21/11, 1129--1164, 1991\n\n"
      "@param maxiter: the number of iterations to perform.\n"
      "@param maxdelta: the maximum distance to move a vertex in\n"
      "  an iteration. C{None} means the number of vertices.\n"
      "@param area: the area of the square on which the vertices\n"
      "  will be placed. C{None} means the square of M{maxdelta}.\n"
      "@param coolexp: the cooling exponent of the simulated annealing.\n"
      "@param repulserad: determines the radius at which vertex-vertex\n"
      "  repulsion cancels out attraction of adjacent vertices.\n"
      "  C{None} means M{maxiter*maxdelta}.\n"
      "@return: the calculated coordinate pairs in a list."
  },
  
  // interface to igraph_layout_fruchterman_reingold_3d
  {"layout_fruchterman_reingold_3d", (PyCFunction)igraphmodule_Graph_layout_fruchterman_reingold_3d,
      METH_VARARGS | METH_KEYWORDS,
      "layout_fruchterman_reingold_3d(maxiter=500, maxdelta=None, area=None, coolexp=0.99, repulserad=maxiter*maxdelta)\n\n"
      "Places the vertices in the 3D space according to the Fruchterman-Reingold grid algorithm.\n\n"
      "This is a force directed layout, see Fruchterman, T. M. J. and Reingold, E. M.:\n"
      "Graph Drawing by Force-directed Placement.\n"
      "Software -- Practice and Experience, 21/11, 1129--1164, 1991\n\n"
      "@param maxiter: the number of iterations to perform.\n"
      "@param maxdelta: the maximum distance to move a vertex in\n"
      "  an iteration. C{None} means the number of vertices.\n"
      "@param area: the area of the square on which the vertices\n"
      "  will be placed. C{None} means the square of M{maxdelta}.\n"
      "@param coolexp: the cooling exponent of the simulated annealing.\n"
      "@param repulserad: determines the radius at which vertex-vertex\n"
      "  repulsion cancels out attraction of adjacent vertices.\n"
      "  C{None} means M{maxiter*maxdelta}.\n"
      "@return: the calculated coordinate triplets in a list."
  },
  
  // interface to igraph_layout_grid_fruchterman_reingold
  {"layout_grid_fruchterman_reingold", (PyCFunction)igraphmodule_Graph_layout_grid_fruchterman_reingold,
      METH_VARARGS | METH_KEYWORDS,
      "layout_grid_fruchterman_reingold(maxiter=500, maxdelta=None, area=None, coolexp=0.99, repulserad=maxiter*maxdelta, cellsize=1.0)\n\n"
      "Places the vertices on a 2D plane according to the Fruchterman-Reingold grid algorithm.\n\n"
      "This is a modified version of a force directed layout, see\n"
      "Fruchterman, T. M. J. and Reingold, E. M.:\n"
      "Graph Drawing by Force-directed Placement.\n"
      "Software -- Practice and Experience, 21/11, 1129--1164, 1991.\n"
      "The algorithm partitions the 2D space to a grid and vertex\n"
      "repulsion is then calculated only for vertices nearby.\n\n"
      "@param maxiter: the number of iterations to perform.\n"
      "@param maxdelta: the maximum distance to move a vertex in\n"
      "  an iteration. C{None} means the number of vertices.\n"
      "@param area: the area of the square on which the vertices\n"
      "  will be placed. C{None} means the square of M{maxdelta}.\n"
      "@param coolexp: the cooling exponent of the simulated annealing.\n"
      "@param repulserad: determines the radius at which vertex-vertex\n"
      "  repulsion cancels out attraction of adjacent vertices.\n"
      "  C{None} means M{maxiter*maxdelta}.\n"
      "@param cellsize: the size of the grid cells.\n"
      "@return: the calculated coordinate pairs in a list."
  },
  
  // interface to igraph_layout_lgl
  {"layout_lgl", (PyCFunction)igraphmodule_Graph_layout_lgl,
      METH_VARARGS | METH_KEYWORDS,
      "layout_lgl(maxiter=500, maxdelta=None, area=None, coolexp=0.99, repulserad=maxiter*maxdelta, cellsize=1.0, proot=None)\n\n"
      "Places the vertices on a 2D plane according to the Large Graph Layout.\n\n"
      "@param maxiter: the number of iterations to perform.\n"
      "@param maxdelta: the maximum distance to move a vertex in\n"
      "  an iteration. C{None} means the number of vertices.\n"
      "@param area: the area of the square on which the vertices\n"
      "  will be placed. C{None} means the square of M{maxdelta}.\n"
      "@param coolexp: the cooling exponent of the simulated annealing.\n"
      "@param repulserad: determines the radius at which vertex-vertex\n"
      "  repulsion cancels out attraction of adjacent vertices.\n"
      "  C{None} means M{maxiter*maxdelta}.\n"
      "@param cellsize: the size of the grid cells.\n"
      "@param proot: the root vertex, this is placed first, its neighbors\n"
      "  in the first iteration, second neighbors in the second,\n"
      "  etc. C{None} means a random vertex.\n"
      "@return: the calculated coordinate pairs in a list."
  },
  
  // interface to igraph_layout_reingold_tilford
  {"layout_reingold_tilford", (PyCFunction)igraphmodule_Graph_layout_reingold_tilford,
      METH_VARARGS | METH_KEYWORDS,
      "layout_reingold_tilford(root)\n"
      "Places the vertices on a 2D plane according to the Reingold-Tilford\n"
      "layout algorithm. See the following reference for details:\n"
      "EM Reingold, JS Tilford: Tidier Drawings of Trees.\n"
      "IEEE Transactions on Software Engineering 7:22, 223-228, 1981.\n\n"
      "@param root: the root of the tree.\n"
      "@return: the calculated coordinate pairs in a list."
  },
  
  // interface to igraph_layout_random
  {"layout_random", (PyCFunction)igraphmodule_Graph_layout_random,
      METH_VARARGS | METH_KEYWORDS,
      "layout_random()\n"
      "Places the vertices of the graph randomly in a 2D space.\n\n"
      "@return: the \"calculated\" coordinate pairs in a list."
  },
   
  // interface to igraph_layout_random_3d
  {"layout_random_3d", (PyCFunction)igraphmodule_Graph_layout_random_3d,
      METH_VARARGS | METH_KEYWORDS,
      "layout_random_3d()\n"
      "Places the vertices of the graph randomly in a 3D space.\n\n"
      "@return: the \"calculated\" coordinate triplets in a list."
  },

  ////////////////////////////
  // VISITOR-LIKE FUNCTIONS //
  ////////////////////////////
  {"bfs", (PyCFunction)igraphmodule_Graph_bfs,
      METH_VARARGS | METH_KEYWORDS,
      "bfs(vid, mode=OUT)\n\n"
      "Conducts a breadth first search (BFS) on the graph.\n\n"
      "@param vid: the root vertex ID\n"
      "@param mode: either C{IN} or C{OUT} or C{ALL}, ignored\n"
      "  for undirected graphs.\n"
      "@return: a tuple with the following items:\n"
      "   - The vertex IDs visited (in order)\n"
      "   - The start indices of the layers in the vertex list\n"
      "   - The parent of every vertex in the BFS\n"
  },
  {"bfsiter", (PyCFunction)igraphmodule_Graph_bfsiter,
      METH_VARARGS | METH_KEYWORDS,
      "bfsiter(vid, mode=OUT, advanced=False)\n\n"
      "Constructs a breadth first search (BFS) iterator of the graph.\n\n"
      "@param vid: the root vertex ID\n"
      "@param mode: either C{IN} or C{OUT} or C{ALL}.\n"
      "@param advanced: if C{False}, the iterator returns the next\n"
      "  vertex in BFS order in every step. If C{True}, the iterator\n"
      "  returns the distance of the vertex from the root and the\n"
      "  parent of the vertex in the BFS tree as well.\n"
      "@return: the BFS iterator as an L{igraph.BFSIter} object.\n"
  },
  
  /////////////////
  // CONVERSIONS //
  /////////////////
  
  // interface to igraph_get_adjacency
  {"get_adjacency", (PyCFunction)igraphmodule_Graph_get_adjacency,
      METH_VARARGS | METH_KEYWORDS,
      "get_adjacency(type=GET_ADJACENCY_BOTH)\n\n"
      "Returns the adjacency matrix of a graph.\n\n"
      "@param type: either C{GET_ADJACENCY_LOWER} (uses the\n"
      "  lower triangle of the matrix) or C{GET_ADJACENCY_UPPER}\n"
      "  (uses the upper triangle) or C{GET_ADJACENCY_BOTH}\n"
      "  (uses both parts). Ignored for directed graphs.\n"
      "@return: the adjacency matrix.\n"
  },
  
  // interface to igraph_get_edgelist
  {"get_edgelist", (PyCFunction)igraphmodule_Graph_get_edgelist,
      METH_NOARGS,
      "get_edgelist()\n\n"
      "Returns the edge list of a graph."
  },
  
  // interface to igraph_to_directed
  {"to_directed", (PyCFunction)igraphmodule_Graph_to_directed,
   METH_VARARGS | METH_KEYWORDS,
   "to_directed(mutual=True)\n\n"
   "Converts an undirected graph to directed.\n\n"
   "@param mutual: C{True} if mutual directed edges should be\n"
   "  created for every undirected edge. If C{False}, a directed\n"
   "  edge with arbitrary direction is created.\n"
  },

  // interface to igraph_to_undirected
  {"to_undirected", (PyCFunction)igraphmodule_Graph_to_undirected,
   METH_VARARGS | METH_KEYWORDS,
   "to_undirected(collapse=True)\n\n"
   "Converts a directed graph to undirected.\n\n"
   "@param collapse: C{True} if only a single edge should be\n"
   "  created from multiple directed edges going between the\n"
   "  same vertex pair. If C{False}, the edge count is kept constant.\n"
  },

  /* interface to igraph_laplacian */
  {"laplacian", (PyCFunction)igraphmodule_Graph_laplacian,
   METH_VARARGS | METH_KEYWORDS,
   "laplacian(normalized=False)\n\n"
   "Returns the Laplacian matrix of a graph.\n\n"
   "The Laplacian matrix is similar to the adjacency matrix, but the edges\n"
   "are denoted with -1 and the diagonal contains the node degrees.\n\n"
   "Normalized Laplacian matrices have 1 or 0 in their diagonals (0 for nodes\n"
   "with no edges), edges are denoted by 1 / sqrt(d_i * d_j) where d_i is the\n"
   "degree of node i.\n\n"
   "Multiple edges and self-loops are silently ignored. Although it is\n"
   "possible to calculate the Laplacian matrix of a directed graph, it does\n"
   "not make much sense.\n\n"
   "@param normalized: whether to return the normalized Laplacian matrix.\n"
   "@return: the Laplacian matrix.\n"
  },
  
  ///////////////////////////////
  // LOADING AND SAVING GRAPHS //
  ///////////////////////////////
  
  // interface to igraph_read_graph_dimacs
  {"Read_DIMACS", (PyCFunction)igraphmodule_Graph_Read_DIMACS,
   METH_VARARGS | METH_KEYWORDS | METH_CLASS,
   "Read_DIMACS(f, directed=False)\n\n"
   "Reads a graph from a file conforming to the DIMACS minimum-cost flow file format\n\n."
   "For the exact description of the format, see\n"
   "X{http://lpsolve.sourceforge.net/5.5/DIMACS.htm}\n\n"
   "Restrictions compared to the official description of the format:\n\n"
   "  * igraph's DIMACS reader requires only three fields in an arc definition,\n"
   "    describing the edge's source and target node and its capacity.\n\n"
   "  * Source nodes are identified by 's' in the FLOW field, target nodes are\n"
   "    identified by 't'.\n\n"
   "  * Node indices start from 1. Only a single source and target node is allowed.\n\n"
   "@param f: the name of the file\n"
   "@param directed: whether the generated graph should be directed.\n"
   "@return: the generated graph, the source and the target of the flow and the edge\n"
   "  capacities in a tuple\n"
  },

  // interface to igraph_read_graph_edgelist
  {"Read_Edgelist", (PyCFunction)igraphmodule_Graph_Read_Edgelist,
      METH_VARARGS | METH_KEYWORDS | METH_CLASS,
      "Read_Edgelist(f, directed=True)\n\n"
      "Reads an edge list from a file and creates a graph based on it.\n\n"
      "Please note that the vertex indices are zero-based.\n\n"
      "@param f: the name of the file\n"
      "@param directed: whether the generated graph should be directed.\n"
  },
  // interface to igraph_read_graph_ncol
  {"Read_Ncol", (PyCFunction)igraphmodule_Graph_Read_Ncol,
      METH_VARARGS | METH_KEYWORDS | METH_CLASS,
      "Read_Ncol(f, names=True, weights=True)\n\n"
      "Reads an .ncol file used by LGL.\n\n"
      "It is also useful for creating graphs from \"named\" (and\n"
      "optionally weighted) edge lists.\n\n"
      "This format is used by the Large Graph Layout program. See the\n"
      "U{documentation of LGL <http://bioinformatics.icmb.utexas.edu/bgl/>}\n"
      "regarding the exact format description.\n\n"
      "LGL originally cannot deal with graphs containing multiple or loop\n"
      "edges, but this condition is not checked here, as igraph is happy\n"
      "with these.\n\n"
      "@param f: the name of the file\n"
      "@param names: If C{True}, the vertex names are added as a\n"
      "  vertex attribute called 'name'.\n"
      "@param weights: If True, the edge weights are added as an\n"
      "  edge attribute called 'weight'.\n"
  },
  // interface to igraph_read_graph_lgl
  {"Read_Lgl", (PyCFunction)igraphmodule_Graph_Read_Lgl,
      METH_VARARGS | METH_KEYWORDS | METH_CLASS,
      "Read_Lgl(f, names=True, weights=True)\n\n"
      "Reads an .lgl file used by LGL.\n\n"
      "It is also useful for creating graphs from \"named\" (and\n"
      "optionally weighted) edge lists.\n\n"
      "This format is used by the Large Graph Layout program. See the\n"
      "U{documentation of LGL <http://bioinformatics.icmb.utexas.edu/bgl/>}\n"
      "regarding the exact format description.\n\n"
      "LGL originally cannot deal with graphs containing multiple or loop\n"
      "edges, but this condition is not checked here, as igraph is happy\n"
      "with these.\n\n"
      "@param f: the name of the file\n"
      "@param names: If C{True}, the vertex names are added as a\n"
      "  vertex attribute called 'name'.\n"
      "@param weights: If True, the edge weights are added as an\n"
      "  edge attribute called 'weight'.\n"
  },
  // interface to igraph_read_graph_pajek
  {"Read_Pajek", (PyCFunction)igraphmodule_Graph_Read_Pajek,
      METH_VARARGS | METH_KEYWORDS | METH_CLASS,
      "Read_Pajek(f)\n\n"
      "Reads a Pajek format file and creates a graph based on it.\n\n"
      "@param f: the name of the file\n"
  },
  // interface to igraph_read_graph_graphml
  {"Read_GraphML", (PyCFunction)igraphmodule_Graph_Read_GraphML,
      METH_VARARGS | METH_KEYWORDS | METH_CLASS,
      "Read_GraphML(f, directed=True, index=0)\n\n"
      "Reads a GraphML format file and creates a graph based on it.\n\n"
      "@param f: the name of the file\n"
      "@param index: if the GraphML file contains multiple graphs,\n"
      "  specifies the one that should be loaded. Graph indices\n"
      "  start from zero, so if you want to load the first graph,\n"
      "  specify 0 here.\n"
  },
  // interface to igraph_write_graph_dimacs
  {"write_dimacs", (PyCFunction)igraphmodule_Graph_write_dimacs,
   METH_VARARGS | METH_KEYWORDS,
   "write_dimacs(f, source, target, capacity=None)\n\n"
   "Writes the graph in DIMACS format to the given file.\n\n"
   "edge list of a graph to a file.\n\n"
   "@param f: the name of the file to be written\n"
   "@param source: the source vertex ID\n"
   "@param target: the target vertex ID\n"
   "@param capacity: the capacities of the edges in a list. If it is not a\n"
   "  list, the corresponding edge attribute will be used to retrieve\n"
   "  capacities."
  },
  // interface to igraph_write_graph_edgelist
  {"write_edgelist", (PyCFunction)igraphmodule_Graph_write_edgelist,
      METH_VARARGS | METH_KEYWORDS,
      "write_edgelist(f)\n\n"
      "Writes the edge list of a graph to a file.\n\n"
      "Directed edges are written in (from, to) order.\n\n"
      "@param f: the name of the file to be written\n"
  },
  // interface to igraph_write_graph_ncol
  {"write_ncol", (PyCFunction)igraphmodule_Graph_write_ncol,
      METH_VARARGS | METH_KEYWORDS,
      "write_ncol(f, names=\"name\", weights=\"weights\")\n\n"
      "Writes the edge list of a graph to a file in .ncol format.\n\n"
      "Note that multiple edges and/or loops break the LGL software,\n"
      "but igraph does not check for this condition. Unless you know\n"
      "that the graph does not have multiple edges and/or loops, it\n"
      "is wise to call L{simplify()} before saving.\n\n"
      "@param f: the name of the file to be written\n"
      "@param names: the name of the vertex attribute containing the name\n"
      "  of the vertices. If you don't want to store vertex names,\n"
      "  supply C{None} here.\n"
      "@param weights: the name of the edge attribute containing the weight\n"
      "  of the vertices. If you don't want to store weights,\n"
      "  supply C{None} here.\n"
  },
  // interface to igraph_write_graph_lgl
  {"write_lgl", (PyCFunction)igraphmodule_Graph_write_lgl,
      METH_VARARGS | METH_KEYWORDS,
      "write_lgl(f, names=\"name\", weights=\"weights\", isolates=True)\n\n"
      "Writes the edge list of a graph to a file in .lgl format.\n\n"
      "Note that multiple edges and/or loops break the LGL software,\n"
      "but igraph does not check for this condition. Unless you know\n"
      "that the graph does not have multiple edges and/or loops, it\n"
      "is wise to call L{simplify()} before saving.\n\n"
      "@param f: the name of the file to be written\n"
      "@param names: the name of the vertex attribute containing the name\n"
      "  of the vertices. If you don't want to store vertex names,\n"
      "  supply C{None} here.\n"
      "@param weights: the name of the edge attribute containing the weight\n"
      "  of the vertices. If you don't want to store weights,\n"
      "  supply C{None} here.\n"
      "@param isolates: whether to include isolated vertices in the output.\n"
  },
  // interface to igraph_write_graph_edgelist
  {"write_graphml", (PyCFunction)igraphmodule_Graph_write_graphml,
      METH_VARARGS | METH_KEYWORDS,
      "write_graphml(f)\n\n"
      "Writes the graph to a GraphML file.\n\n"
      "@param f: the name of the file to be written\n"
  },

  /////////////////
  // ISOMORPHISM //
  /////////////////
  {"isoclass", (PyCFunction)igraphmodule_Graph_isoclass,
      METH_VARARGS | METH_KEYWORDS,
      "isoclass(vertices)\n\n"
      "Returns the isomorphy class of the graph or its subgraph.\n\n"
      "Isomorphy class calculations are implemented only for graphs with\n"
      "3 or 4 nodes.\n\n"
      "@param vertices: a list of vertices if we want to calculate the\n"
      "  isomorphy class for only a subset of vertices. C{None} means to\n"
      "  use the full graph.\n"
      "@return: the isomorphy class of the (sub)graph\n\n"
  },
  {"isomorphic", (PyCFunction)igraphmodule_Graph_isomorphic,
      METH_VARARGS | METH_KEYWORDS,
      "isomorphic(other)\n\n"
      "Checks whether the graph is isomorphic with another graph.\n\n"
      "Works only for graphs with 3 or 4 vertices.\n\n"
      "@param other: the other graph with which we want to compare the graph.\n"
      "@return: C{True} if the graphs are isomorphic, C{False} if not.\n"
  },
  
  ////////////////////////
  // ATTRIBUTE HANDLING //
  ////////////////////////
  {"attributes", (PyCFunction)igraphmodule_Graph_attributes,
      METH_NOARGS,
      "attributes()\n\n"
      "@return: the attribute name list of the graph\n"
  },
  {"vertex_attributes", (PyCFunction)igraphmodule_Graph_vertex_attributes,
      METH_NOARGS,
      "vertex_attributes()\n\n"
      "@return: the attribute name list of the graph's vertices\n"
  },
  {"edge_attributes", (PyCFunction)igraphmodule_Graph_edge_attributes,
      METH_NOARGS,
      "edge_attributes()\n\n"
      "@return: the attribute name list of the graph's edges\n"
  },

  ///////////////
  // OPERATORS //
  ///////////////
  {"complementer", (PyCFunction)igraphmodule_Graph_complementer,
      METH_VARARGS,
      "complementer(loops=False)\n\n"
      "Returns the complementer of the graph\n\n"
      "@param loops: whether to include loop edges in the complementer.\n"
      "@return: the complementer of the graph\n"
  },
  {"compose", (PyCFunction)igraphmodule_Graph_compose,
      METH_O, "compose(other)\n\nReturns the composition of two graphs."
  },
  {"difference", (PyCFunction)igraphmodule_Graph_difference,
      METH_O, "difference(other)\n\nSubtracts the given graph from the original"
  },
  {"disjoint_union", (PyCFunction)igraphmodule_Graph_disjoint_union,
      METH_O,
      "disjoint_union(graphs)\n\n"
      "Creates the disjoint union of two (or more) graphs.\n\n"
      "@param graphs: the list of graphs to be united with the current one.\n"
  },
  {"intersection", (PyCFunction)igraphmodule_Graph_intersection,
      METH_O,
      "intersection(graphs)\n\n"
      "Creates the intersection of two (or more) graphs.\n\n"
      "@param graphs: the list of graphs to be intersected with\n"
      "  the current one.\n"
  },
  {"union", (PyCFunction)igraphmodule_Graph_union,
      METH_O,
      "union(graphs)\n\n"
      "Creates the union of two (or more) graphs.\n\n"
      "@param graphs: the list of graphs to be intersected with\n"
      "  the current one.\n"
  },

  /**************************/
  /* FLOW RELATED FUNCTIONS */  
  /**************************/
  {"maxflow_value", (PyCFunction)igraphmodule_Graph_maxflow_value,
   METH_VARARGS | METH_KEYWORDS,
   "maxflow_value(source, target, capacity=None)\n\n"
   "Returns the maximum flow between the source and target vertices.\n\n"
   "@param source: the source vertex ID\n"
   "@param target: the target vertex ID\n"
   "@param capacity: the capacity of the edges. It must be a list or a valid\n"
   "  attribute name or C{None}. In the latter case, every edge will have the\n"
   "  same capacity.\n"
   "@return: the value of the maximum flow between the given vertices\n"
  },

  {"mincut_value", (PyCFunction)igraphmodule_Graph_mincut_value,
   METH_VARARGS | METH_KEYWORDS,
   "mincut_value(source=-1, target=-1, capacity=None)\n\n"
   "Returns the minimum cut between the source and target vertices.\n\n"
   "@param source: the source vertex ID. If negative, the calculation is\n"
   "  done for every vertex except the target and the minimum is returned.\n"
   "@param target: the target vertex ID. If negative, the calculation is\n"
   "  done for every vertex except the source and the minimum is returned.\n"
   "@param capacity: the capacity of the edges. It must be a list or a valid\n"
   "  attribute name or C{None}. In the latter case, every edge will have the\n"
   "  same capacity.\n"
   "@return: the value of the minimum cut between the given vertices\n"
  },

  ////////////////////////////////////
  // INTERNAL/DEVELOPMENT FUNCTIONS //
  ////////////////////////////////////
  {"__graph_as_cobject", (PyCFunction)igraphmodule_Graph___graph_as_cobject__,
      METH_VARARGS | METH_KEYWORDS,
      "__graph_as_cobject()\n\n"
      "Returns the igraph graph encapsulated by the Python object as\n"
      "a PyCObject\n\n."
      "A PyObject is barely a regular C pointer. This function\n"
      "should not be used directly by igraph users, it is useful only\n"
      "in the case when the underlying igraph object must be passed to\n"
      "another C code through Python.\n\n"
  },
  {"__register_destructor", (PyCFunction)igraphmodule_Graph___register_destructor__,
      METH_VARARGS | METH_KEYWORDS,
      "__register_destructor(destructor)\n\n"
      "Registers a destructor to be called when the object is freed by\n"
      "Python. This function should not be used directly by igraph users."
  },
  {NULL}

};

/** \ingroup python_interface_graph
 * This structure is the collection of functions necessary to implement
 * the graph as a mapping (i.e. to allow the retrieval and setting of
 * igraph attributes in Python as if it were of a Python mapping type)
 */
PyMappingMethods igraphmodule_Graph_as_mapping = {
  // returns the number of graph attributes
  (inquiry)igraphmodule_Graph_attribute_count,
  // returns an attribute by name
  (binaryfunc)igraphmodule_Graph_get_attribute,
  // sets an attribute by name
  (objobjargproc)igraphmodule_Graph_set_attribute
};

/** \ingroup python_interface
 * \brief Collection of methods to allow numeric operators to be used on the graph
 */
PyNumberMethods igraphmodule_Graph_as_number = {
    (binaryfunc)igraphmodule_Graph_disjoint_union, /* nb_add */
    (binaryfunc)igraphmodule_Graph_difference,	/*nb_subtract*/
    0,	/*nb_multiply*/
    0,	/*nb_divide*/
    0,	/*nb_remainder*/
    0,	/*nb_divmod*/
    0,	/*nb_power*/
    0,	/*nb_negative*/
    0,	/*nb_positive*/
    0,	/*nb_absolute*/
    0,	/*nb_nonzero*/
    (unaryfunc)igraphmodule_Graph_complementer_op,	/*nb_invert*/
    0,	/*nb_lshift*/
    0,	/*nb_rshift*/
    (binaryfunc)igraphmodule_Graph_intersection, /*nb_and*/
    0,	/*nb_xor*/
    (binaryfunc)igraphmodule_Graph_union,	 /*nb_or*/
    0,	/*nb_coerce*/
    0,	/*nb_int*/
    0,	/*nb_long*/
    0,	/*nb_float*/
    0,	/*nb_oct*/
    0, 	/*nb_hex*/
    0,	/*nb_inplace_add*/
    0,	/*nb_inplace_subtract*/
    0,	/*nb_inplace_multiply*/
    0,	/*nb_inplace_divide*/
    0,	/*nb_inplace_remainder*/
    0,	/*nb_inplace_power*/
    0,	/*nb_inplace_lshift*/
    0,	/*nb_inplace_rshift*/
    0,	/*nb_inplace_and*/
    0,	/*nb_inplace_xor*/
    0,	/*nb_inplace_or*/
};

/** \ingroup python_interface_graph
 * Python type object referencing the methods Python calls when it performs various operations on an igraph (creating, printing and so on)
 */
PyTypeObject igraphmodule_GraphType = {
  PyObject_HEAD_INIT(NULL)
  0,                                        /* ob_size */
  "igraph.Graph",                           /* tp_name */
  sizeof(igraphmodule_GraphObject),         /* tp_basicsize */
  0,                                        /* tp_itemsize */
  (destructor)igraphmodule_Graph_dealloc,   /* tp_dealloc */
  0,                                        /* tp_print */
  0,                                        /* tp_getattr */
  0,                                        /* tp_setattr */
  0,                                        /* tp_compare */
  0,                                        /* tp_repr */
  &igraphmodule_Graph_as_number,            /* tp_as_number */
  0,                                        /* tp_as_sequence */
  &igraphmodule_Graph_as_mapping,           /* tp_as_mapping */
  0,                                        /* tp_hash */
  0,                                        /* tp_call */
  (reprfunc)igraphmodule_Graph_str,         /* tp_str */
  0,                                        /* tp_getattro */
  0,                                        /* tp_setattro */
  0,                                        /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC, /* tp_flags */
  "Class representing a graph in the igraph library.",                    /* tp_doc */
  (traverseproc)igraphmodule_Graph_traverse,/* tp_traverse */
  (inquiry)igraphmodule_Graph_clear,        /* tp_clear */
  0,                                        /* tp_richcompare */
  offsetof(igraphmodule_GraphObject, weakreflist), /* tp_weaklistoffset */
  0,                                        /* tp_iter */
  0,                                        /* tp_iternext */
  &igraphmodule_Graph_methods,              /* tp_methods */
  0,                                        /* tp_members */
  &igraphmodule_Graph_getseters,            /* tp_getset */
  0,                                        /* tp_base */
  0,                                        /* tp_dict */
  0,                                        /* tp_descr_get */
  0,                                        /* tp_descr_set */
  0,                                        /* tp_dictoffset */
  (initproc)igraphmodule_Graph_init,        /* tp_init */
  0,                                        /* tp_alloc */
  igraphmodule_Graph_new,                   /* tp_new */
  0,                                        /* tp_free */
};