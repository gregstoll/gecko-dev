/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import kotlinx.coroutines.test.runTest
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.state.state.createTab
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.support.test.libstate.ext.waitUntilIdle
import mozilla.components.support.test.rule.MainCoroutineRule
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify
import org.mozilla.fenix.components.bookmarks.BookmarksUseCase.AddBookmarksUseCase
import org.mozilla.fenix.components.menu.fake.FakeBookmarksStorage
import org.mozilla.fenix.components.menu.middleware.MenuDialogMiddleware
import org.mozilla.fenix.components.menu.store.BookmarkState
import org.mozilla.fenix.components.menu.store.BrowserMenuState
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.menu.store.MenuState
import org.mozilla.fenix.components.menu.store.MenuStore

class MenuDialogMiddlewareTest {

    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()
    private val scope = coroutinesTestRule.scope

    private val bookmarksStorage: BookmarksStorage = FakeBookmarksStorage()
    private val addBookmarkUseCase: AddBookmarksUseCase =
        spy(AddBookmarksUseCase(storage = bookmarksStorage))

    @Test
    fun `GIVEN selected tab is bookmarked WHEN init action is dispatched THEN initial bookmark state is updated`() = runTest {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"

        val guid = bookmarksStorage.addItem(
            parentGuid = BookmarkRoot.Mobile.id,
            url = url,
            title = title,
            position = 5u,
        )

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val store = createStore(
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
        )

        assertNull(store.state.browserMenuState!!.bookmarkState.guid)
        assertFalse(store.state.browserMenuState!!.bookmarkState.isBookmarked)

        store.dispatch(MenuAction.InitAction).join()
        store.waitUntilIdle()

        assertEquals(guid, store.state.browserMenuState!!.bookmarkState.guid)
        assertTrue(store.state.browserMenuState!!.bookmarkState.isBookmarked)
    }

    @Test
    fun `GIVEN selected tab is not bookmarked WHEN init action is dispatched THEN initial bookmark state is not updated`() = runTest {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val store = createStore(
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
        )

        assertNull(store.state.browserMenuState!!.bookmarkState.guid)
        assertFalse(store.state.browserMenuState!!.bookmarkState.isBookmarked)

        store.dispatch(MenuAction.InitAction).join()
        store.waitUntilIdle()

        assertNull(store.state.browserMenuState!!.bookmarkState.guid)
        assertFalse(store.state.browserMenuState!!.bookmarkState.isBookmarked)
    }

    @Test
    fun `WHEN add bookmark action is dispatched THEN bookmark state is updated`() = runTest {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"
        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val store = createStore(
            menuState = MenuState(
                browserMenuState = browserMenuState,
            ),
        )

        store.waitUntilIdle()

        assertNull(store.state.browserMenuState!!.bookmarkState.guid)
        assertFalse(store.state.browserMenuState!!.bookmarkState.isBookmarked)

        store.dispatch(MenuAction.AddBookmark).join()
        store.waitUntilIdle()

        assertNotNull(store.state.browserMenuState!!.bookmarkState.guid)
        assertTrue(store.state.browserMenuState!!.bookmarkState.isBookmarked)
    }

    @Test
    fun `GIVEN no browser state WHEN add bookmark action is dispatched THEN bookmark state is not updated`() = runTest {
        val store = createStore(
            menuState = MenuState(
                browserMenuState = null,
            ),
        )

        assertNull(store.state.browserMenuState)

        store.dispatch(MenuAction.AddBookmark).join()
        store.waitUntilIdle()

        assertNull(store.state.browserMenuState)
    }

    @Test
    fun `GIVEN selected tab is bookmarked WHEN add bookmark action is dispatched THEN bookmark state is not updated`() = runTest {
        val url = "https://www.mozilla.org"
        val title = "Mozilla"

        val guid = bookmarksStorage.addItem(
            parentGuid = BookmarkRoot.Mobile.id,
            url = url,
            title = title,
            position = 5u,
        )

        val browserMenuState = BrowserMenuState(
            selectedTab = createTab(
                url = url,
                title = title,
            ),
        )
        val store = spy(
            createStore(
                menuState = MenuState(
                    browserMenuState = browserMenuState,
                ),
            ),
        )

        store.waitUntilIdle()

        assertNotNull(store.state.browserMenuState!!.bookmarkState.guid)
        assertTrue(store.state.browserMenuState!!.bookmarkState.isBookmarked)

        store.dispatch(MenuAction.AddBookmark).join()
        store.waitUntilIdle()

        verify(addBookmarkUseCase, never()).invoke(url = url, title = title)
        verify(store, never()).dispatch(
            MenuAction.UpdateBookmarkState(
                bookmarkState = BookmarkState(
                    guid = guid,
                    isBookmarked = true,
                ),
            ),
        )

        assertNotNull(store.state.browserMenuState!!.bookmarkState.guid)
        assertTrue(store.state.browserMenuState!!.bookmarkState.isBookmarked)
    }

    private fun createStore(
        menuState: MenuState = MenuState(),
    ) = MenuStore(
        initialState = menuState,
        middleware = listOf(
            MenuDialogMiddleware(
                bookmarksStorage = bookmarksStorage,
                addBookmarkUseCase = addBookmarkUseCase,
                scope = scope,
            ),
        ),
    )
}
