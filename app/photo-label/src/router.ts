import Vue from 'vue';
import Router from 'vue-router';
//import Home from './views/Home.vue';
import ModePicker from './views/ModePicker.vue';
import Label from './views/Label.vue';
import Report from './views/Report.vue';

Vue.use(Router);

// NOTE: If you add new routes here, you also need to add exceptions for them inside Server.cpp (in Server::ServeStatic)
export default new Router({
	mode: 'history',
	base: process.env.BASE_URL,
	routes: [
		{
			path: '/',
			name: 'modePicker',
			component: ModePicker,
		},
		{
			path: '/report',
			name: 'report',
			component: Report,
		},
		{
			path: '/label/:dimid',
			name: 'label',
			component: Label,
			props: true,
		},
		{
			path: '/about',
			name: 'about',
			// route level code-splitting
			// this generates a separate chunk (about.[hash].js) for this route
			// which is lazy-loaded when the route is visited.
			component: () => import(/* webpackChunkName: "about" */ './views/About.vue'),
		},
	],
});
