import unittest
from igraph import *

class GameTests(unittest.TestCase):
    def testGRG(self):
	g=Graph.GRG(50, 0.2)
	self.failUnless(isinstance(g, Graph))
	g=Graph.GRG(50, 0.2, True)
	self.failUnless(isinstance(g, Graph))

    def testRecentDegree(self):
	g=Graph.Recent_Degree(100, 5, 10)
	self.failUnless(isinstance(g, Graph))

    def testPreference(self):
	g=Graph.Preference(100, [1, 1], [[1, 0], [0, 1]])
	self.failUnless(isinstance(g, Graph) and max(g.clusters()) == 1)

	g=Graph.Preference(100, [1, 1], [[1, 0], [0, 1]], attribute="type")
	l=g.vs.get_attribute_values("type")
	self.failUnless(min(l) == 0 and max(l) == 1)

    def testAsymmetricPreference(self):
	g=Graph.Asymmetric_Preference(100, [[0, 1], [1, 0]], [[0, 1], [1, 0]])
	self.failUnless(isinstance(g, Graph) and max(g.clusters()) == 1)

	g=Graph.Asymmetric_Preference(100, [[0, 1], [1, 0]], [[1, 0], [0, 1]],\
				      attribute="type")
	l=g.vs.get_attribute_values("type")
	l1=[i[0] for i in l]
	l2=[i[1] for i in l]
	self.failUnless(min(l1) == 0 and max(l1) == 1 and
			min(l2) == 0 and max(l2) == 1)

	g=Graph.Asymmetric_Preference(100, [[0, 1], [1, 0]], [[1, 0], [0, 1]])
	self.failUnless(isinstance(g, Graph) and max(g.clusters()) == 0)

def suite():
    game_suite = unittest.makeSuite(GameTests)
    return unittest.TestSuite((game_suite))

def test():
    runner = unittest.TextTestRunner()
    runner.run(suite())
    
if __name__ == "__main__":
    test()
