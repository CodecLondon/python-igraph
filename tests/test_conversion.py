import unittest
from igraph import *

class DirectedUndirectedTests(unittest.TestCase):
    def testToUndirected(self):
        graph = Graph([(0,1), (0,2), (1,0)], directed=True)

        graph2 = graph.copy()
        graph2.to_undirected(mode=False)
        self.assertTrue(graph2.vcount() == graph.vcount())
        self.assertTrue(graph2.is_directed() == False)
        self.assertTrue(sorted(graph2.get_edgelist()) == [(0,1), (0,1), (0,2)])

        graph2 = graph.copy()
        graph2.to_undirected()
        self.assertTrue(graph2.vcount() == graph.vcount())
        self.assertTrue(graph2.is_directed() == False)
        self.assertTrue(sorted(graph2.get_edgelist()) == [(0,1), (0,2)])

        graph2 = graph.copy()
        graph2.es["weight"] = [1,2,3]
        graph2.to_undirected(mode="collapse", combine_edges="sum")
        self.assertTrue(graph2.vcount() == graph.vcount())
        self.assertTrue(graph2.is_directed() == False)
        self.assertTrue(sorted(graph2.get_edgelist()) == [(0,1), (0,2)])
        self.assertTrue(graph2.es["weight"] == [4,2])

        graph = Graph([(0,1),(1,0),(0,1),(1,0),(2,1),(1,2)], directed=True)
        graph2 = graph.copy()
        graph2.es["weight"] = [1,2,3,4,5,6]
        graph2.to_undirected(mode="mutual", combine_edges="sum")
        self.assertTrue(graph2.vcount() == graph.vcount())
        self.assertTrue(graph2.is_directed() == False)
        self.assertTrue(sorted(graph2.get_edgelist()) == [(0,1), (0,1), (1,2)])
        self.assertTrue(graph2.es["weight"] == [7,3,11] or graph2.es["weight"] == [3,7,11])

    def testToDirected(self):
        graph = Graph([(0,1), (0,2), (2,3), (4,2)], directed=False)
        graph.to_directed()
        self.assertTrue(graph.is_directed())
        self.assertTrue(graph.vcount() == 5)
        self.assertTrue(sorted(graph.get_edgelist()) == \
                [(0,1), (0,2), (1,0), (2,0), (2,3), (2,4), (3,2), (4,2)]
        )

    def testToDirectedAcyclic(self):
        graph = Graph([(0,1), (0,2), (2,3), (4,2)], directed=False)
        graph.to_directed(mode="acyclic")
        self.assertTrue(graph.is_directed())
        self.assertTrue(graph.vcount() == 5)
        self.assertTrue(sorted(graph.get_edgelist()) == [(0,1), (0,2), (2,3), (2,4)])

    def testToDirectedArbitrary(self):
        graph = Graph([(0,1), (0,2), (2,3), (4,2)], directed=False)
        graph.to_directed(mode="acyclic")
        self.assertTrue(graph.is_directed())
        self.assertTrue(graph.vcount() == 5)

        el = graph.get_edgelist()
        self.assertTrue(len(el) == 4)
        self.assertTrue((0,1) in el or (1,0) in el)
        self.assertTrue((0,2) in el or (2,0) in el)
        self.assertTrue((2,3) in el or (3,2) in el)
        self.assertTrue((2,4) in el or (4,2) in el)

    def testToDirectedMutual(self):
        graph = Graph([(0,1), (0,2), (2,3), (4,2)], directed=False)
        graph.to_directed(mode="mutual")
        self.assertTrue(graph.is_directed())
        self.assertTrue(graph.vcount() == 5)
        self.assertTrue(sorted(graph.get_edgelist()) == \
                [(0,1), (0,2), (1,0), (2,0), (2,3), (2,4), (3,2), (4,2)]
        )

    def testToDirectedRandom(self):
        edge_lists = []
        for i in range(100):
            graph = Graph([(0,1), (0,2), (2,3), (4,2)], directed=False)
            graph.to_directed(mode="random")
            self.assertTrue(graph.is_directed())
            self.assertTrue(graph.vcount() == 5)

            el = graph.get_edgelist()
            self.assertTrue(len(el) == 4)
            self.assertTrue((0,1) in el or (1,0) in el)
            self.assertTrue((0,2) in el or (2,0) in el)
            self.assertTrue((2,3) in el or (3,2) in el)
            self.assertTrue((2,4) in el or (4,2) in el)

            edge_lists.append(tuple(el))

        self.assertTrue(len(set(edge_lists)) > 1)

    def testToDirectedLegacyMutualArgument(self):
        graph = Graph([(0,1), (0,2), (2,3), (2,4)], directed=False)
        graph.to_directed(mutual=True)
        self.assertTrue(graph.is_directed())
        self.assertTrue(graph.vcount() == 5)
        self.assertTrue(sorted(graph.get_edgelist()) == \
                [(0,1), (0,2), (1,0), (2,0), (2,3), (2,4), (3,2), (4,2)]
        )

        graph = Graph([(0,1), (0,2), (2,3), (2,4)], directed=False)
        graph.to_directed(mutual=False)
        self.assertTrue(graph.is_directed())
        self.assertTrue(graph.vcount() == 5)
        self.assertTrue(sorted(graph.get_edgelist()) == [(0,1), (0,2), (2,3), (2,4)])


class GraphRepresentationTests(unittest.TestCase):
    def testGetAdjacency(self):
        # Undirected case
        g = Graph.Tree(6, 3)
        g.es["weight"] = range(5)
        self.assertTrue(g.get_adjacency() == Matrix([
            [0, 1, 1, 1, 0, 0],
            [1, 0, 0, 0, 1, 1],
            [1, 0, 0, 0, 0, 0],
            [1, 0, 0, 0, 0, 0],
            [0, 1, 0, 0, 0, 0],
            [0, 1, 0, 0, 0, 0]
        ]))
        self.assertTrue(g.get_adjacency(attribute="weight") == Matrix([
            [0, 0, 1, 2, 0, 0],
            [0, 0, 0, 0, 3, 4],
            [1, 0, 0, 0, 0, 0],
            [2, 0, 0, 0, 0, 0],
            [0, 3, 0, 0, 0, 0],
            [0, 4, 0, 0, 0, 0]
        ]))
        self.assertTrue(g.get_adjacency(eids=True) == Matrix([
            [0, 1, 2, 3, 0, 0],
            [1, 0, 0, 0, 4, 5],
            [2, 0, 0, 0, 0, 0],
            [3, 0, 0, 0, 0, 0],
            [0, 4, 0, 0, 0, 0],
            [0, 5, 0, 0, 0, 0]
        ])-1)

        # Directed case
        g = Graph.Tree(6, 3, "tree_out")
        g.add_edges([(0,1), (1,0)])
        self.assertTrue(g.get_adjacency() == Matrix([
            [0, 2, 1, 1, 0, 0],
            [1, 0, 0, 0, 1, 1],
            [0, 0, 0, 0, 0, 0],
            [0, 0, 0, 0, 0, 0],
            [0, 0, 0, 0, 0, 0],
            [0, 0, 0, 0, 0, 0]
        ]))

    def testGetSparseAdjacency(self):
        try:
            from scipy import sparse
            import numpy as np
        except ImportError:
            self.skipTest("Scipy and numpy are dependencies of this test.")

        # Undirected case
        g = Graph.Tree(6, 3)
        g.es["weight"] = range(5)
        self.assertTrue(np.all(
            (g.get_adjacency_sparse() == np.array(g.get_adjacency().data))
        ))
        self.assertTrue(np.all(
            (g.get_adjacency_sparse(attribute="weight") == np.array(g.get_adjacency(attribute="weight").data))
        ))

        # Directed case
        g = Graph.Tree(6, 3, "tree_out")
        g.add_edges([(0,1), (1,0)])
        self.assertTrue(np.all(
            g.get_adjacency_sparse() == np.array(g.get_adjacency().data)
        ))


def suite():
    direction_suite = unittest.makeSuite(DirectedUndirectedTests)
    representation_suite = unittest.makeSuite(GraphRepresentationTests)
    return unittest.TestSuite([direction_suite,
        representation_suite])

def test():
    runner = unittest.TextTestRunner()
    runner.run(suite())
    
if __name__ == "__main__":
    test()

